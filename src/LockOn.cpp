#include "LockOn.h"

#include "Game.h"
#include "Marker.h"
#include "Reflect.h"
#include "Settings.h"
#include "Ue.h"

// ================================================================================================================
// A port of Ultimate Combat Redux's lock-on (UltimateCombatAMF\lua\Scripts\systems\LockOn.lua - Kramer7046's Ultimate
// Combat 2.7, modified under his Nexus permissions, which allow modification with credit). The owner, 2026-10-01, after
// CCM's own-design lock-on aimed low: "This should be simple to fix since you already have a working example. Literally
// just copy it over and add the toggle for auto pointing at the head with the bow and the chest for melee weapons."
//
// Ported as written: the targets (every BP_PairedPawnAIController_C's pawn, alive, not the player, within the range by
// GetDistanceTo, within the search angle by the yaw from the camera, in line of sight, the smallest yaw difference
// first), the instant toggle with its 0.35 s debounce, the camera drive (the third-person arm frozen - no inherited
// rotation, absolute - on engage and put back on release; every tick FindLookAtRotation from the camera to the aim
// socket, a step of the difference times the tracking speed capped at 300 deg/s yaw and 180 deg/s pitch - 210 / 135 and
// a speed of at most 0.35 in the 0.6 s action window after an attack - the pitch clamped to +-75, written to the
// controller's ControlRotation and to the arm's RelativeRotation), the sockets (Head_Socket / Spine_Socket /
// Pelvis_Socket, Root_Socket when the skeleton lacks one; three ticks with no aim point release), the body-part cycle
// (wraps round, 0.15 s apart), the target switch (within the switch angle of the current target's yaw, in line of sight,
// the nearest that side, 0.20 s apart, the body part kept), the checks every 0.4 s (dead, three line-of-sight failures,
// beyond the range x 1.1), the camera-origin sanity release, the first-person release, the release when sheathing, and
// the sounds (UI events 4 on lock, 11 on release, 3 on a switch or an aim-point move).
//
// Kept from CCM: the key and button rows (Left Alt / R3 - R3 is the owner's target-lock button), the stand-down when
// Ultimate Combat's own lock-on is switched on, the target's name over it (the selection marker, handed over), the
// right stick flicks for the controller (UE's stick-direction keys, read from XInput past the same dead zone), and our
// own addition, "Aim point by weapon": a bow aims at the Head, a melee weapon at the Spine.
// ================================================================================================================

namespace lockon
{
	namespace
	{
		using namespace reflect;
		using Clock = std::chrono::steady_clock;

		constexpr const wchar_t* kAIControllerClass = L"/Game/Dev/Controllers/BP_PairedPawnAIController.BP_PairedPawnAIController_C";
		constexpr double kToggleDebounce = 0.35;
		constexpr double kSocketCooldown = 0.15;
		constexpr double kCheckEvery = 0.4;
		constexpr int    kLosGraceFails = 3;
		constexpr int    kAimFailRelease = 3;
		constexpr double kRangeRelease = 1.1;
		constexpr double kBadCameraZ = 2000.0, kBadCameraDistance = 5000.0;
		constexpr double kYawRate = 300.0, kPitchRate = 180.0;               // deg/s
		constexpr double kActionYawRate = 210.0, kActionPitchRate = 135.0;   // deg/s, in the action window
		constexpr double kActionSpeedCap = 0.35;
		constexpr double kSnapDelta = 45.0;
		constexpr double kDtMin = 1.0 / 60.0, kDtMax = 0.050;
		constexpr double kPitchLimit = 75.0;
		constexpr float  kStickPress = 8689.0f / 32767.0f;   // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE: where UE's stick-direction keys press
		constexpr int    kSoundLock = 4, kSoundRelease = 11, kSoundSwitch = 3;

		// The body parts, as Ultimate Combat names and orders them: 0 Head, 1 Spine, 2 Pelvis.
		constexpr const wchar_t* kPartSockets[3] = { L"Head_Socket", L"Spine_Socket", L"Pelvis_Socket" };
		constexpr const char*    kPartNames[3] = { "Head", "Spine", "Pelvis" };
		constexpr const wchar_t* kSocketFallback = L"Root_Socket";

		struct Target
		{
			ue::Handle    pawn;          // the target's Unreal pawn
			ue::Handle    mesh;          // its MainSkeletalMeshComponent
			std::wstring  socket;        // the socket aimed at ("" = none found)
			RE::TESFormID ref = 0;       // its Oblivion reference, for the marker (0 when not matched)
			std::string   name;
		};

		Target      g_target;
		bool        g_locked = false;
		int         g_part = 1;
		bool        g_partManual = false;
		int         g_weaponKind = -1;
		bool        g_toggleQueued = false;
		int         g_partQueued = 0;
		int         g_targetQueued = 0;
		bool        g_stickRight = false, g_stickLeft = false, g_stickUp = false, g_stickDown = false;
		double      g_lastToggle = -10.0, g_lastSwitchTarget = -10.0, g_lastSwitchSocket = -10.0, g_lastCheck = 0.0, g_lastStep = 0.0;
		int         g_losFails = 0, g_aimFails = 0;
		bool        g_wasDrawn = false;
		ue::Handle  g_playerPawn, g_arm;
		bool        g_armFrozen = false;
		std::array<bool, 4> g_armSaved{};

		std::mutex  g_lock;
		std::string g_status = "not locked";
		std::string g_shownTarget, g_lastRelease;
		std::uint32_t g_engages = 0, g_switches = 0;
		std::atomic<bool> g_ucrOn{ false };

		double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

		void SetStatus(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			g_status = std::move(a_s);
		}

		double Normal(double a)
		{
			a = std::fmod(a, 360.0);
			if (a > 180.0) a -= 360.0;
			if (a < -180.0) a += 360.0;
			return a;
		}
		double Shortest(double a, double b) { return std::fmod(std::fmod(b - a + 540.0, 360.0) + 360.0, 360.0) - 180.0; }

		// FindLookAtRotation from a to b (Unreal's world): pitch, yaw, roll 0
		std::array<double, 3> LookAt(const std::array<double, 3>& a_from, const std::array<double, 3>& a_to)
		{
			const double dx = a_to[0] - a_from[0], dy = a_to[1] - a_from[1], dz = a_to[2] - a_from[2];
			return { std::atan2(dz, std::hypot(dx, dy)) * 180.0 / std::numbers::pi, std::atan2(dy, dx) * 180.0 / std::numbers::pi, 0.0 };
		}

		bool Location(UE::UObject* a_actor, std::array<double, 3>& a_out)
		{
			static ue::Getter location(L"K2_GetActorLocation");
			return a_actor && location.Get(a_actor, a_out);
		}

		bool IsDead(UE::UObject* a_pawn)
		{
			ue::Call c(a_pawn, L"IsDead");
			if (!c || !c.Run()) return false;
			return c.Get<bool>("ReturnValue");
		}

		// pawn:GetDistanceTo(other), Unreal units (UE 5.3 returns a double; a float build is read as one)
		double DistanceTo(UE::UObject* a_from, UE::UObject* a_to)
		{
			ue::Call c(a_from, L"GetDistanceTo");
			if (!c) return 1e12;
			c.Set("OtherActor", a_to);
			if (!c.Run()) return 1e12;
			const double d = c.Get<double>("ReturnValue");   // UE 5.3 returns a double (large world coordinates)
			if (std::isfinite(d) && d >= 0.0 && d < 1e9) return d;
			return static_cast<double>(c.Get<float>("ReturnValue"));
		}

		// pc:LineOfSightTo(pawn, camera, false)
		bool LineOfSight(UE::UObject* a_ctrl, UE::UObject* a_pawn, const std::array<double, 3>& a_from)
		{
			if (!settings::Get().lockOnLineOfSight) return true;
			ue::Call c(a_ctrl, L"LineOfSightTo");
			if (!c) return true;
			c.Set("Other", a_pawn);
			c.Set("ViewPoint", a_from);
			c.Set("bAlternateChecks", false);
			if (!c.Run()) return true;
			return c.Get<bool>("ReturnValue");
		}

		bool SocketExists(UE::UObject* a_mesh, const wchar_t* a_name)
		{
			ue::Call c(a_mesh, L"DoesSocketExist");
			if (!c) return false;
			c.Set("InSocketName", UE::FName(a_name, UE::EFindName::Add));
			c.Run();
			return c.Get<bool>("ReturnValue");
		}

		void PickSocket(UE::UObject* a_pawn)
		{
			g_target.mesh = {};
			g_target.socket.clear();
			auto* mesh = a_pawn ? GetObject(a_pawn, Find(a_pawn, "MainSkeletalMeshComponent")) : nullptr;
			if (!mesh) return;
			g_target.mesh.Set(mesh);
			if (SocketExists(mesh, kPartSockets[g_part])) {
				g_target.socket = kPartSockets[g_part];
			} else if (SocketExists(mesh, kSocketFallback)) {
				g_target.socket = kSocketFallback;
			}
		}

		std::string SocketName()
		{
			if (g_target.socket.empty()) return "no socket";
			return g_target.socket == kSocketFallback ? std::string("Root_Socket (no ") + kPartNames[g_part] + "_Socket)" : std::string(kPartNames[g_part]) + "_Socket";
		}

		bool AimPoint(std::array<double, 3>& a_out)
		{
			auto* mesh = g_target.socket.empty() ? nullptr : g_target.mesh.Get();
			if (!mesh) return false;
			ue::Call c(mesh, L"GetSocketLocation");
			if (!c) return false;
			c.Set("InSocketName", UE::FName(g_target.socket.c_str(), UE::EFindName::Add));
			c.Run();
			a_out = c.Get<std::array<double, 3>>("ReturnValue");
			return a_out[0] != 0.0 || a_out[1] != 0.0 || a_out[2] != 0.0;
		}

		// the held weapon's kind (WeaponsPairingComponent -> WeaponActor -> WeaponTypeTag): 0 none / other, 1 bow, 2 melee
		int WeaponKind(UE::UObject* a_pawn)
		{
			auto* wpc = a_pawn ? GetObject(a_pawn, Find(a_pawn, "WeaponsPairingComponent")) : nullptr;
			auto* weapon = wpc ? GetObject(wpc, Find(wpc, "WeaponActor")) : nullptr;
			const std::string tag = weapon ? GetName(weapon, Find(weapon, "WeaponTypeTag.TagName")) : std::string();
			if (tag.empty() || tag.find("Staff") != std::string::npos) return 0;
			return tag.find("Bow") != std::string::npos ? 1 : 2;
		}

		// "Aim point by weapon" (ours): a bow Head, a melee weapon Spine, anything else the starting aim point
		int PartFor(int a_kind, const settings::Values& a_s)
		{
			if (a_s.lockOnAimByWeapon && a_kind == 1) return 0;
			if (a_s.lockOnAimByWeapon && a_kind == 2) return 1;
			return std::clamp(a_s.lockOnStartPart, 1, 3) - 1;
		}

		const char* WhyPart(int a_kind, const settings::Values& a_s)
		{
			if (a_s.lockOnAimByWeapon && a_kind == 1) return "bow";
			if (a_s.lockOnAimByWeapon && a_kind == 2) return "melee weapon";
			return "the starting aim point";
		}

		// a UI sound by Ultimate Combat's id (its systems/Sound.lua table), posted on the player's pawn
		void PlaySound(int a_id, UE::UObject* a_pawn)
		{
			if (!settings::Get().lockOnPlaySound || !a_pawn) return;
			static const wchar_t* kPaths[] = {
				L"/Game/WwiseAudio/Interface/Global/Redesign/ui_glb_popup_yes.ui_glb_popup_yes",              // 3
				L"/Game/WwiseAudio/Interface/Global/Redesign/ui_glb_hover.ui_glb_hover",                      // 4
				L"/Game/WwiseAudio/Interface/Other/Redesign/ui_quickkeys_hover.ui_quickkeys_hover",           // 11
			};
			const wchar_t* path = a_id == 3 ? kPaths[0] : a_id == 4 ? kPaths[1] : a_id == 11 ? kPaths[2] : nullptr;
			auto* ev = path ? UE::StaticFindObject(nullptr, nullptr, path) : nullptr;
			if (!ev || !ue::IsLive(ev)) return;   // not loaded this session: silent, as Ultimate Combat's
			ue::Call post(ev, L"PostOnActor");
			if (!post) return;
			post.Set("Actor", a_pawn);
			post.Set("bStopWhenAttachedToDestroyed", false);
			post.Run();
		}

		// the Oblivion reference whose paired Unreal pawn this is (for the marker), from the cells around the player
		RE::TESObjectREFR* RefOfPawn(UE::UObject* a_pawn, RE::PlayerCharacter* a_player)
		{
			if (!a_pawn || !a_player || !a_player->parentCell) return nullptr;
			std::vector<RE::TESObjectCELL*> cells{ a_player->parentCell };
			if (!a_player->GetInterior()) {
				if (auto* world = a_player->GetWorldSpace(); world && world->cellMap) {
					const auto& p = a_player->data.location;
					const int   cx = static_cast<int>(std::floor(p.x / 4096.0f)), cy = static_cast<int>(std::floor(p.y / 4096.0f));
					for (int dx = -1; dx <= 1; ++dx) {
						for (int dy = -1; dy <= 1; ++dy) {
							const std::int32_t key = static_cast<std::int32_t>((static_cast<std::uint32_t>(cx + dx) << 16) | (static_cast<std::uint32_t>(cy + dy) & 0xFFFF));
							const auto it = world->cellMap->find(key);
							if (it != world->cellMap->end() && it->second && std::ranges::find(cells, it->second) == cells.end()) cells.push_back(it->second);
						}
					}
				}
			}
			for (auto* cell : cells) {
				for (auto* ref : cell->listReferences) {
					const auto type = ref ? ref->GetFormType() : RE::FormType::None;
					if (type != RE::FormType::ActorCharacter && type != RE::FormType::ActorCreature) continue;
					auto* entry = static_cast<RE::IVPairableItem*>(ref)->pairingEntry;
					if (entry && entry->isPaired && entry->hostItem == a_pawn) return ref;
				}
			}
			return nullptr;
		}

		std::string NameOf(UE::UObject* a_pawn, RE::TESObjectREFR* a_ref)
		{
			if (a_ref) {
				auto*       base = a_ref->data.objectReference;
				const char* n = base ? RE::TESFullName::GetFullName(base) : nullptr;
				return std::format("{} [{:08X}]", n && *n ? n : "(no name)", a_ref->GetFormID());
			}
			return a_pawn ? ue::NameOf(a_pawn) : std::string("?");
		}

		struct Candidate
		{
			UE::UObject*          pawn = nullptr;
			std::array<double, 3> at{};
			double                yaw = 0;    // from the camera
			double                score = 0;  // |yaw difference| from the camera's own yaw
		};

		// every AI controller's pawn that can be locked (alive, not the player, within the range); yaw and score from the camera
		std::vector<Candidate> Gather(UE::UObject* a_player, const std::array<double, 3>& a_cam, double a_camYaw, double a_range)
		{
			std::vector<Candidate> out;
			static auto* cls = ue::Class(kAIControllerClass);
			if (!cls) cls = ue::Class(kAIControllerClass);
			if (!cls) return out;
			for (auto* ctrl : ue::AllOf(cls)) {
				auto* pawn = GetObject(ctrl, Find(ctrl, "Pawn"));
				if (!pawn || pawn == a_player || IsDead(pawn)) continue;
				if (DistanceTo(a_player, pawn) > a_range) continue;
				Candidate c{ .pawn = pawn };
				if (!Location(pawn, c.at)) continue;
				c.yaw = LookAt(a_cam, c.at)[1];
				c.score = std::abs(Shortest(a_camYaw, c.yaw));
				out.push_back(c);
			}
			return out;
		}

		void FreezeArm(UE::UObject* a_arm)
		{
			if (!a_arm || g_armFrozen) return;
			g_armSaved = { GetBool(a_arm, Find(a_arm, "bInheritPitch")), GetBool(a_arm, Find(a_arm, "bInheritYaw")), GetBool(a_arm, Find(a_arm, "bInheritRoll")),
				GetBool(a_arm, Find(a_arm, "bAbsoluteRotation")) };
			SetBool(a_arm, Find(a_arm, "bInheritYaw"), false);
			SetBool(a_arm, Find(a_arm, "bInheritPitch"), false);
			SetBool(a_arm, Find(a_arm, "bInheritRoll"), false);
			SetBool(a_arm, Find(a_arm, "bAbsoluteRotation"), true);
			g_arm.Set(a_arm);
			g_armFrozen = true;
		}

		void RestoreArm()
		{
			if (!g_armFrozen) return;
			g_armFrozen = false;
			auto* arm = g_arm.Get();
			if (!arm) return;
			SetBool(arm, Find(arm, "bInheritPitch"), g_armSaved[0]);
			SetBool(arm, Find(arm, "bInheritYaw"), g_armSaved[1]);
			SetBool(arm, Find(arm, "bInheritRoll"), g_armSaved[2]);
			SetBool(arm, Find(arm, "bAbsoluteRotation"), g_armSaved[3]);
		}

		void Release(const char* a_why, UE::UObject* a_playerPawn)
		{
			if (!g_locked) return;
			g_locked = false;
			logger::info("lock-on: let go of {} - {}", g_target.name, a_why);
			PlaySound(kSoundRelease, a_playerPawn);
			RestoreArm();
			g_target = {};
			marker::Lock(nullptr);
			{
				std::scoped_lock l(g_lock);
				g_lastRelease = a_why;
				g_shownTarget.clear();
			}
			SetStatus(std::format("not locked (last: {})", a_why));
		}

		void SetTarget(UE::UObject* a_pawn, RE::PlayerCharacter* a_player)
		{
			g_target.pawn.Set(a_pawn);
			auto* ref = RefOfPawn(a_pawn, a_player);
			g_target.ref = ref ? ref->GetFormID() : 0;
			g_target.name = NameOf(a_pawn, ref);
			PickSocket(a_pawn);
			g_losFails = 0;
			g_aimFails = 0;
			std::scoped_lock l(g_lock);
			g_shownTarget = g_target.name;
		}

		void Engage(const Candidate& a_c, const In& a_in, RE::PlayerCharacter* a_player, int a_kind, const settings::Values& a_s)
		{
			auto* arm = GetObject(a_in.pawn, Find(a_in.pawn, "ThirdPersonCameraSpringArmComponent"));
			if (!arm) {
				logger::info("lock-on: no third-person arm - not locked");
				return;
			}
			g_part = PartFor(a_kind, a_s);
			g_partManual = false;
			g_weaponKind = a_kind;
			SetTarget(a_c.pawn, a_player);
			g_locked = true;
			g_lastCheck = Now();
			g_lastStep = 0.0;
			FreezeArm(arm);
			++g_engages;
			PlaySound(kSoundLock, a_in.pawn);
			logger::info("lock-on: locked on {} ({:.1f} degrees off the camera; aim point {} - {})", g_target.name, a_c.score, SocketName(), WhyPart(a_kind, a_s));
			SetStatus("locked on " + g_target.name);
		}

		// Ultimate Combat's step_rotation: the difference times the speed, each axis capped at its rate x dt
		std::array<double, 3> Step(const std::array<double, 3>& a_cur, const std::array<double, 3>& a_goal, double a_speed, double a_pitchMax, double a_yawMax)
		{
			const double cp = Normal(a_cur[0]), gp = Normal(a_goal[0]), cy = Normal(a_cur[1]), gy = Normal(a_goal[1]);
			const double dp = Shortest(cp, gp), dyaw = Shortest(cy, gy);
			double sp = dp * a_speed, sy = dyaw * a_speed;
			// the snap guard (a difference of 45 or more) uses the snap rates - the same numbers as the normal ones
			(void)kSnapDelta;
			sp = std::clamp(sp, -a_pitchMax, a_pitchMax);
			sy = std::clamp(sy, -a_yawMax, a_yawMax);
			return { std::clamp(Normal(cp + sp), -kPitchLimit, kPitchLimit), Normal(cy + sy), 0.0 };
		}

		// [Lock-On] in Ultimate Combat's INI: the section whose VarName is LockOnEnabled, its value (read at most every 2 s)
		bool ReadUltimateCombatSwitch()
		{
			static bool      s_on = false;
			static ULONGLONG s_next = 0;
			const ULONGLONG  now = GetTickCount64();
			if (now < s_next) return s_on;
			s_next = now + 2000;
			static const auto path = settings::PluginFolder().parent_path().parent_path() / L"MadConfigs" / L"Ultimate Combat.ini";
			bool on = false;
			std::FILE* f = nullptr;
			if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
				std::string text;
				char        buf[4096];
				for (std::size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
				std::fclose(f);
				const auto var = text.find("VarName=LockOnEnabled");
				if (var != std::string::npos) {
					const auto start = text.rfind("\n[", var);
					auto       end = text.find("\n[", var);
					if (end == std::string::npos) end = text.size();
					const auto from = start == std::string::npos ? 0 : start;
					const auto section = std::string_view(text).substr(from, end - from);
					const auto v = section.find("\nvalue=");
					on = v != std::string_view::npos && v + 7 < section.size() && section[v + 7] == '1';
				}
			}
			g_ucrOn = on;
			if (on != s_on) logger::info("lock-on: Ultimate Combat's own lock-on is {} in its settings{}", on ? "ON" : "off", on ? " - CCM's lock-on stands down" : "");
			s_on = on;
			return s_on;
		}
	}

	void Toggle() { g_toggleQueued = true; }
	void MoveAimPoint(int a_dir) { g_partQueued = a_dir < 0 ? -1 : 1; }
	void MoveTarget(int a_dir) { g_targetQueued = a_dir < 0 ? -1 : 1; }
	bool Active() { return g_locked; }
	bool UltimateCombatOwnsIt() { return g_ucrOn.load(); }

	void Tick(const In& a_in)
	{
		const auto& s = settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const double now = Now();
		bool        pressed = std::exchange(g_toggleQueued, false);
		int         partMove = std::exchange(g_partQueued, 0);
		int         targetMove = std::exchange(g_targetQueued, 0);

		if (g_playerPawn.Get() != a_in.pawn) {   // a load: nothing carries over
			g_playerPawn.Set(a_in.pawn);
			Release("a new game was loaded", a_in.pawn);
			g_armFrozen = false;
		}
		const bool  ucrOn = ReadUltimateCombatSwitch();
		const bool  dialogue = a_in.cameraTag && a_in.cameraTag->find("Dialogue") != std::string::npos;
		const char* blocked = !s.enabled ? "CCM is off" : !s.lockOnEnabled ? "the lock-on is switched off" : ucrOn ? "Ultimate Combat's own lock-on is on" :
		                      a_in.firstPerson ? "first-person active" : dialogue ? "a conversation" : nullptr;
		if (blocked) {
			Release(blocked, a_in.pawn);
			if (pressed) logger::info("lock-on: the key was pressed - nothing to do ({})", blocked);
			if (!g_locked) SetStatus(std::format("not available - {}", blocked));
			return;
		}
		if (a_in.ucrLocked) {
			Release("Ultimate Combat locked on", a_in.pawn);
			return;
		}
		if (!player || !a_in.ctrl || !a_in.mgr || !a_in.pawn) return;

		// the release when sheathing (Ultimate Combat's cancel_on_sheathe): the weapon put away while locked
		const bool drawn = GetBool(a_in.pawn, Find(a_in.pawn, "bInCombatStance"));
		if (g_locked && s.lockOnCancelOnSheathe && g_wasDrawn && !drawn) {
			Release("weapon sheathed", a_in.pawn);
		}
		g_wasDrawn = drawn;

		// the instant toggle, debounced 0.35 s
		if (pressed && now - g_lastToggle < kToggleDebounce) {
			logger::debug("lock-on: instant toggle ignored - debounce");
			pressed = false;
		}
		if (pressed) g_lastToggle = now;
		if (pressed && g_locked) {
			Release("toggled off", a_in.pawn);
			return;
		}

		std::array<double, 3> cam{}, camRot{}, me{};
		static ue::Getter camLocation(L"GetCameraLocation"), camRotation(L"GetCameraRotation"), controlRotation(L"GetControlRotation");
		if (!camLocation.Get(a_in.mgr, cam) || !camRotation.Get(a_in.mgr, camRot) || !Location(a_in.pawn, me)) return;
		const double range = s.lockOnRange;
		const int    kind = WeaponKind(a_in.pawn);

		if (pressed) {
			if (s.lockOnWeaponsDrawnOnly && !drawn) {
				logger::info("lock-on: engage refused - weapons not drawn");
			} else {
				const auto       all = Gather(a_in.pawn, cam, camRot[1], range);
				const Candidate* best = nullptr;
				for (const auto& c : all) {
					if (c.score > s.lockOnAngle) continue;
					if (!LineOfSight(a_in.ctrl, c.pawn, cam)) continue;
					if (!best || c.score < best->score) best = &c;
				}
				if (best) {
					Engage(*best, a_in, player, kind, s);
				} else {
					logger::info("lock-on: no target in front of camera ({} within range, search angle {:.0f})", all.size(), s.lockOnAngle);
					SetStatus("not locked - no target in front of the camera");
				}
			}
		}
		if (!g_locked) return;

		auto* target = g_target.pawn.Get();
		if (!target) {
			Release("invalid target", a_in.pawn);
			return;
		}

		// a menu is up: hold the lock, turn nothing, mark nothing
		auto* im = RE::InterfaceManager::GetInstance(false, false);
		if (!im || im->menuMode != 1) {
			marker::Lock(nullptr);
			return;
		}

		// the controller's right stick, read as UE's stick-direction keys (pressed past the stick's dead zone)
		float rx = 0.0f, ry = 0.0f;
		if ((s.lockOnStickSwitch || s.lockOnStickAimPoint) && game::PadRightStick(rx, ry)) {
			const bool right = rx > kStickPress, left = rx < -kStickPress, up = ry > kStickPress, down = ry < -kStickPress;
			if (s.lockOnStickSwitch && right && !g_stickRight) targetMove = 1;
			if (s.lockOnStickSwitch && left && !g_stickLeft) targetMove = -1;
			if (s.lockOnStickAimPoint && up && !g_stickUp) partMove = -1;
			if (s.lockOnStickAimPoint && down && !g_stickDown) partMove = 1;
			g_stickRight = right;
			g_stickLeft = left;
			g_stickUp = up;
			g_stickDown = down;
		}

		// the target switch: within the switch angle of the current target's yaw, in line of sight, the nearest that side
		if (targetMove != 0 && now - g_lastSwitchTarget >= s.lockOnSwitchCooldown) {
			g_lastSwitchTarget = now;
			std::array<double, 3> curAt{};
			if (Location(target, curAt)) {
				const double     curYaw = LookAt(cam, curAt)[1];
				const Candidate* best = nullptr;
				double           bestDelta = 1e9;
				const auto       all = Gather(a_in.pawn, cam, camRot[1], range);
				for (const auto& c : all) {
					if (c.pawn == target) continue;
					const double d = Shortest(curYaw, c.yaw);
					if (std::abs(d) > s.lockOnSwapAngle || std::abs(d) <= 0.1) continue;
					if ((targetMove > 0 && d <= 0.1) || (targetMove < 0 && d >= -0.1)) continue;
					if (!LineOfSight(a_in.ctrl, c.pawn, cam)) continue;
					if (std::abs(d) < bestDelta) {
						best = &c;
						bestDelta = std::abs(d);
					}
				}
				if (best) {
					SetTarget(best->pawn, player);
					target = best->pawn;
					++g_switches;
					PlaySound(kSoundSwitch, a_in.pawn);
					logger::info("lock-on: switched {} to {} (delta {:.1f}; aim point {})", targetMove > 0 ? "right" : "left", g_target.name, bestDelta, SocketName());
					SetStatus("locked on " + g_target.name);
				} else {
					logger::debug("lock-on: target switch skipped - no candidate that side");
				}
			}
		}

		// the weapon changed while locked: aim point by weapon again (a body part moved by the player is dropped)
		if (kind != g_weaponKind) {
			const int was = g_weaponKind;
			g_weaponKind = kind;
			if (s.lockOnAimByWeapon && was >= 0) {
				const int part = PartFor(kind, s);
				g_partManual = false;
				if (part != g_part) {
					g_part = part;
					PickSocket(target);
					logger::info("lock-on: aim point {} - {}", SocketName(), WhyPart(kind, s));
				}
			}
		}
		// up or down a body part, wrapping round (up Pelvis -> Spine -> Head, down Head -> Spine -> Pelvis), 0.15 s apart
		if (partMove != 0 && now - g_lastSwitchSocket >= kSocketCooldown) {
			g_lastSwitchSocket = now;
			g_part = (g_part + partMove + 3) % 3;
			g_partManual = true;
			PickSocket(target);
			PlaySound(kSoundSwitch, a_in.pawn);
			logger::info("lock-on: aim point {} - moved {}", SocketName(), partMove < 0 ? "up" : "down");
		}

		// the aim point: three ticks without one release
		std::array<double, 3> aim{};
		if (!AimPoint(aim)) {
			if (++g_aimFails >= kAimFailRelease) Release("lost aim socket", a_in.pawn);
			return;
		}
		g_aimFails = 0;

		// the camera's origin must be near the player (a load, a cutscene)
		const double cdz = cam[2] - me[2];
		if (std::abs(cdz) > kBadCameraZ || std::hypot(cam[0] - me[0], cam[1] - me[1], cdz) > kBadCameraDistance) {
			Release("bad camera origin", a_in.pawn);
			return;
		}

		// the camera drive: from the controller's rotation toward the look-at from the camera to the aim point
		std::array<double, 3> cur{};
		if (!controlRotation.Get(a_in.ctrl, cur)) return;
		const auto   goal = LookAt(cam, aim);
		const bool   action = game::ActionWindowActive();
		const double speed = std::clamp(static_cast<double>(s.lockOnSmoothSpeed), 0.01, 1.0);
		const double dt = std::clamp(g_lastStep > 0.0 ? now - g_lastStep : kDtMin, kDtMin, kDtMax);
		g_lastStep = now;
		const auto rot = Step(cur, goal, action ? std::min(speed, kActionSpeedCap) : speed, (action ? kActionPitchRate : kPitchRate) * dt,
			(action ? kActionYawRate : kYawRate) * dt);
		auto* arm = g_arm.Get();
		if (!arm) {
			Release("third-person arm unavailable", a_in.pawn);
			return;
		}
		SetVec(a_in.ctrl, Find(a_in.ctrl, "ControlRotation"), rot);
		SetVec(arm, Find(arm, "RelativeRotation"), rot);

		// every 0.4 s: dead, line of sight (three failures), out of range (x 1.1)
		if (now - g_lastCheck > kCheckEvery) {
			g_lastCheck = now;
			if (IsDead(target)) {
				Release("target dead", a_in.pawn);
				return;
			}
			if (s.lockOnLineOfSight) {
				if (!LineOfSight(a_in.ctrl, target, cam)) {
					if (++g_losFails >= kLosGraceFails) {
						Release("lost line of sight", a_in.pawn);
						return;
					}
				} else {
					g_losFails = 0;
				}
			}
			if (DistanceTo(a_in.pawn, target) > range * kRangeRelease) {
				Release("out of range", a_in.pawn);
				return;
			}
		}

		// the marker over the target (by its Oblivion reference)
		RE::TESObjectREFR* ref = nullptr;
		if (g_target.ref) {
			auto* form = RE::TESForm::LookupByID(g_target.ref);
			const auto type = form ? form->GetFormType() : RE::FormType::None;
			if (type == RE::FormType::ActorCharacter || type == RE::FormType::ActorCreature) ref = static_cast<RE::TESObjectREFR*>(form);
		}
		marker::Lock(s.lockOnMarker ? ref : nullptr);
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		return g_status;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "target", g_shownTarget }, { "last_release", g_lastRelease }, { "engages", g_engages }, { "switches", g_switches },
			{ "aim_point", kPartNames[std::clamp(g_part, 0, 2)] }, { "aim_point_moved_by_player", g_partManual },
			{ "ultimate_combat_lock_on_switched_on", g_ucrOn.load() } };
	}
}
