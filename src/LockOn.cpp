#include "LockOn.h"

#include "Game.h"
#include "Marker.h"
#include "Reflect.h"
#include "Settings.h"
#include "Ue.h"

namespace lockon
{
	namespace
	{
		using namespace reflect;

		constexpr float  kCellSize = 4096.0f;
		constexpr double kChestLift = 45.0;          // cm above the target's capsule centre: its chest, not its belt
		constexpr double kReleaseMargin = 1.25;      // a lock lets go past the range plus a quarter (no flicker at the edge)
		constexpr double kPitchMin = -50.0, kPitchMax = 30.0;
		constexpr float  kFlick = 0.70f, kRearm = 0.30f;   // the right stick: a flick past 70%, armed again under 30%
		constexpr ULONGLONG kNoBodyMs = 500;         // the target's body unreadable this long: let go

		struct Target
		{
			RE::TESFormID id = 0;
			ue::Handle    pawn;
			ue::Handle    mesh;          // its skeletal mesh, for the aim socket
			std::wstring  socket;        // the socket aimed at ("" = the body's centre)
			double        socketDrop = 0; // cm below the socket (the head socket stands in for the chest)
			std::string   name;
		};

		Target      g_target;
		bool        g_aimed = false;                 // a rotation of ours has been written since the lock began
		std::array<double, 3> g_lastRot{};
		bool        g_toggleQueued = false;
		bool        g_stickArmed = true;
		ULONGLONG   g_noBodySince = 0;
		ue::Handle  g_playerPawn;

		std::mutex  g_lock;                          // for Status / State on other threads
		std::string g_status = "not locked";
		std::string g_shownTarget, g_lastRelease;
		std::uint32_t g_engages = 0, g_switches = 0;
		std::atomic<bool> g_ucrOn{ false };

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

		std::string NameOf(RE::TESObjectREFR* a_ref)
		{
			auto*       base = a_ref ? a_ref->data.objectReference : nullptr;
			const char* n = base ? RE::TESFullName::GetFullName(base) : nullptr;
			return std::format("{} [{:08X}]", n && *n ? n : "(no name)", a_ref ? a_ref->GetFormID() : 0);
		}

		// an actor that can be locked: a person or creature in the world, alive, not the player
		RE::Actor* Lockable(RE::TESForm* a_form, RE::PlayerCharacter* a_player)
		{
			// the form's type first: only then is it a reference with a cell to read
			const auto type = a_form ? a_form->GetFormType() : RE::FormType::None;
			if (type != RE::FormType::ActorCharacter && type != RE::FormType::ActorCreature) return nullptr;
			auto* actor = static_cast<RE::Actor*>(a_form);
			if (actor == a_player || actor->IsDeleted() || (actor->GetFormFlags() & RE::TESForm::RecordFlags::kDisabled) || !actor->parentCell) return nullptr;
			return actor->IsDead(false) ? nullptr : actor;
		}

		RE::Actor* ActorById(RE::TESFormID a_id, RE::PlayerCharacter* a_player)
		{
			return a_id ? Lockable(RE::TESForm::LookupByID(a_id), a_player) : nullptr;
		}

		// the actor's Unreal body, through the game's own pairing (the reference's pairing entry holds its actor)
		UE::UObject* BodyOf(RE::TESObjectREFR* a_ref)
		{
			auto* entry = a_ref ? static_cast<RE::IVPairableItem*>(a_ref)->pairingEntry : nullptr;
			return entry && entry->isPaired && entry->hostItem && ue::IsLive(entry->hostItem) ? entry->hostItem : nullptr;
		}

		bool Location(UE::UObject* a_actor, std::array<double, 3>& a_out)
		{
			static ue::Getter location(L"K2_GetActorLocation");
			return a_actor && location.Get(a_actor, a_out);
		}

		bool SocketExists(UE::UObject* a_mesh, const wchar_t* a_name)
		{
			ue::Call c(a_mesh, L"DoesSocketExist");
			if (!c) return false;
			c.Set("InSocketName", UE::FName(a_name, UE::EFindName::Add));
			c.Run();
			return c.Get<bool>("ReturnValue");
		}

		// The point on the target's skeleton the camera looks at (round 2, the owner, 2026-10-01: "CCM's lock on selected the
		// name above the character instead of ... their skeleton" - the capsule centre plus a lift aimed near the name): its
		// spine socket; else its head socket less a drop to the chest; else the body's centre. Chosen once per target.
		void PickSocket(UE::UObject* a_body)
		{
			g_target.mesh = {};
			g_target.socket.clear();
			g_target.socketDrop = 0;
			auto* mesh = a_body ? GetObject(a_body, Find(a_body, "MainSkeletalMeshComponent")) : nullptr;
			if (!mesh) return;
			g_target.mesh.Set(mesh);
			if (SocketExists(mesh, L"Spine_Socket")) {
				g_target.socket = L"Spine_Socket";
			} else if (SocketExists(mesh, L"Head_Socket")) {
				g_target.socket = L"Head_Socket";
				g_target.socketDrop = 35.0;
			}
		}

		// where to look this frame (Unreal's world); false when neither the socket nor the body can be read
		bool AimPoint(UE::UObject* a_body, std::array<double, 3>& a_out)
		{
			if (auto* mesh = g_target.socket.empty() ? nullptr : g_target.mesh.Get()) {
				ue::Call c(mesh, L"GetSocketLocation");
				if (c) {
					c.Set("InSocketName", UE::FName(g_target.socket.c_str(), UE::EFindName::Add));
					c.Run();
					a_out = c.Get<std::array<double, 3>>("ReturnValue");
					if (a_out[0] != 0.0 || a_out[1] != 0.0 || a_out[2] != 0.0) {
						a_out[2] -= g_target.socketDrop;
						return true;
					}
				}
			}
			return Location(a_body, a_out);   // the body's centre: no lift - above it is where the name stands
		}

		// the cells around the player: its own inside, the 3 x 3 around it outside
		std::vector<RE::TESObjectCELL*> NearbyCells(RE::PlayerCharacter* a_player)
		{
			std::vector<RE::TESObjectCELL*> cells;
			auto* cell = a_player->parentCell;
			if (!cell) return cells;
			cells.push_back(cell);
			if (a_player->GetInterior()) return cells;
			auto* world = a_player->GetWorldSpace();
			if (!world || !world->cellMap) return cells;
			const auto& p = a_player->data.location;
			const int   cx = static_cast<int>(std::floor(p.x / kCellSize)), cy = static_cast<int>(std::floor(p.y / kCellSize));
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					const std::int32_t key = static_cast<std::int32_t>((static_cast<std::uint32_t>(cx + dx) << 16) | (static_cast<std::uint32_t>(cy + dy) & 0xFFFF));
					const auto it = world->cellMap->find(key);
					if (it != world->cellMap->end() && it->second && std::ranges::find(cells, it->second) == cells.end()) cells.push_back(it->second);
				}
			}
			return cells;
		}

		struct Candidate
		{
			RE::Actor*            actor = nullptr;
			UE::UObject*          body = nullptr;
			std::array<double, 3> at{};      // Unreal world, the body's centre
			double                dist = 0;  // Oblivion units from the player
			double                yaw = 0;   // the direction from the player's body, degrees (Unreal)
			double                off = 0;   // degrees from the camera's aim (3D)
			bool                  hostile = false;
		};

		// every lockable actor within a_range of the player whose body can be read
		std::vector<Candidate> Gather(RE::PlayerCharacter* a_player, const std::array<double, 3>& a_me, const std::array<double, 3>& a_cam,
			const std::array<double, 3>& a_camFwd, double a_range)
		{
			std::vector<Candidate> out;
			const auto& me = a_player->data.location;
			for (auto* cell : NearbyCells(a_player)) {
				for (auto* ref : cell->listReferences) {
					auto* actor = Lockable(ref, a_player);
					if (!actor) continue;
					const auto& p = actor->data.location;
					const double dist = std::hypot(p.x - me.x, p.y - me.y, p.z - me.z);
					if (dist > a_range) continue;
					Candidate c{ .actor = actor, .dist = dist };
					c.body = BodyOf(actor);
					if (!c.body || !Location(c.body, c.at)) continue;
					c.yaw = std::atan2(c.at[1] - a_me[1], c.at[0] - a_me[0]) * 180.0 / std::numbers::pi;
					const double tx = c.at[0] - a_cam[0], ty = c.at[1] - a_cam[1], tz = c.at[2] + kChestLift - a_cam[2];
					const double tl = std::hypot(tx, ty, tz);
					if (tl < 1.0) continue;
					const double cosOff = (tx * a_camFwd[0] + ty * a_camFwd[1] + tz * a_camFwd[2]) / tl;
					c.off = std::acos(std::clamp(cosOff, -1.0, 1.0)) * 180.0 / std::numbers::pi;
					c.hostile = actor->GetCombatTarget() == a_player;
					out.push_back(c);
				}
			}
			return out;
		}

		std::array<double, 3> Forward(const std::array<double, 3>& a_rot)   // FRotator (pitch, yaw, roll) -> unit vector
		{
			const double p = a_rot[0] * std::numbers::pi / 180.0, y = a_rot[1] * std::numbers::pi / 180.0;
			return { std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) };
		}

		void Release(const char* a_why)
		{
			if (!g_target.id) return;
			logger::info("lock-on: let go of {} - {}", g_target.name, a_why);
			{
				std::scoped_lock l(g_lock);
				g_lastRelease = a_why;
			}
			g_target = {};
			g_aimed = false;
			marker::Lock(nullptr);
			{
				std::scoped_lock l(g_lock);
				g_shownTarget.clear();
			}
			SetStatus(std::format("not locked (last: {})", a_why));
		}

		void Engage(const Candidate& a_c, const char* a_how)
		{
			g_target.id = a_c.actor->GetFormID();
			g_target.pawn.Set(a_c.body);
			g_target.name = NameOf(a_c.actor);
			PickSocket(a_c.body);
			g_noBodySince = 0;
			logger::info("lock-on: {} {} ({:.0f} units, {:.0f} degrees off the aim{}; aimed at {})", a_how, g_target.name, a_c.dist, a_c.off,
				a_c.hostile ? ", fighting you" : "", g_target.socket.empty() ? std::string("the body's centre") :
				std::string(g_target.socket == L"Spine_Socket" ? "Spine_Socket" : "Head_Socket") + (g_target.socketDrop > 0 ? " less 35 cm" : ""));
			{
				std::scoped_lock l(g_lock);
				g_shownTarget = g_target.name;
			}
			SetStatus("locked on " + g_target.name);
		}

		// the best target to take: inside the cone, a hostile fighting you first, then the nearest the middle of the view
		const Candidate* Best(const std::vector<Candidate>& a_all, double a_maxAngle, double a_range)
		{
			const Candidate* best = nullptr;
			double           bestScore = 0;
			for (const auto& c : a_all) {
				if (c.off > a_maxAngle) continue;
				const double score = c.off / std::max(1.0, a_maxAngle) + 0.4 * c.dist / std::max(1.0, a_range) - (c.hostile ? 0.5 : 0.0);
				if (!best || score < bestScore) {
					best = &c;
					bestScore = score;
				}
			}
			return best;
		}

		// the next target to the side the stick was flicked: the smallest turn that way from the current one
		const Candidate* NextToward(const std::vector<Candidate>& a_all, double a_fromYaw, int a_side)
		{
			const Candidate* best = nullptr;
			double           bestTurn = 0;
			for (const auto& c : a_all) {
				if (c.actor->GetFormID() == g_target.id) continue;
				const double turn = Normal(c.yaw - a_fromYaw) * a_side;   // Unreal's yaw grows to the right
				if (turn <= 2.0 || turn > 120.0) continue;
				if (!best || turn < bestTurn) {
					best = &c;
					bestTurn = turn;
				}
			}
			return best;
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
					const auto section = std::string_view(text).substr(start == std::string::npos ? 0 : start, end - (start == std::string::npos ? 0 : start));
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

	void Toggle()
	{
		g_toggleQueued = true;
	}

	bool Active()
	{
		return g_target.id != 0;
	}

	bool UltimateCombatOwnsIt()
	{
		return g_ucrOn.load();   // any thread: the game thread's last read
	}

	void Tick(const In& a_in)
	{
		const auto& s = settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const bool  pressed = std::exchange(g_toggleQueued, false);

		// a new pawn is a load: nothing from before carries over
		if (g_playerPawn.Get() != a_in.pawn) {
			g_playerPawn.Set(a_in.pawn);
			Release("a new game was loaded");
		}
		const bool ucrOn = ReadUltimateCombatSwitch();
		const char* blocked = !s.enabled ? "CCM is off" : !s.lockOnEnabled ? "the lock-on is switched off" : ucrOn ? "Ultimate Combat's own lock-on is on" :
		                      a_in.firstPerson ? "first person" : a_in.cameraTag && a_in.cameraTag->find("Dialogue") != std::string::npos ? "a conversation" : nullptr;
		if (blocked) {
			Release(blocked);
			if (pressed) logger::info("lock-on: the key was pressed - nothing to do ({})", blocked);
			if (!Active()) SetStatus(std::format("not available - {}", blocked));
			return;
		}
		if (a_in.ucrLocked) {
			Release("Ultimate Combat locked on");
			return;
		}
		if (pressed && Active()) {
			Release("the key was pressed again");
			return;
		}
		if (!player || !a_in.ctrl || !a_in.mgr || !a_in.pawn) return;

		std::array<double, 3> me{}, cam{}, camRot{};
		static ue::Getter camLocation(L"GetCameraLocation"), camRotation(L"GetCameraRotation");
		if (!Location(a_in.pawn, me) || !camLocation.Get(a_in.mgr, cam) || !camRotation.Get(a_in.mgr, camRot)) return;

		const double range = s.lockOnRange;
		if (pressed) {
			const auto all = Gather(player, me, cam, Forward(camRot), range);
			if (const auto* best = Best(all, s.lockOnAngle, range)) {
				++g_engages;
				Engage(*best, "locked on");
			} else {
				logger::info("lock-on: the key was pressed - no one within {:.0f} units and {:.0f} degrees of the aim ({} in range)", range, s.lockOnAngle, all.size());
				SetStatus("not locked - no one to lock on");
			}
		}
		if (!Active()) return;

		// the target: still there, alive, in range?
		auto* actor = ActorById(g_target.id, player);
		if (!actor) {
			Release("the target died or is gone");
			return;
		}
		const auto& tp = actor->data.location;
		const auto& pp = player->data.location;
		if (std::hypot(tp.x - pp.x, tp.y - pp.y, tp.z - pp.z) > range * kReleaseMargin) {
			Release("the target is out of range");
			return;
		}
		auto* body = g_target.pawn.Get();
		if (!body) {
			body = BodyOf(actor);   // the body can be rebuilt (a cell reload): look it up again
			if (body) {
				g_target.pawn.Set(body);
				PickSocket(body);
			}
		}
		std::array<double, 3> at{}, aim{};
		if (!body || !Location(body, at) || !AimPoint(body, aim)) {
			const ULONGLONG now = GetTickCount64();
			if (!g_noBodySince) g_noBodySince = now;
			if (now - g_noBodySince >= kNoBodyMs) Release("the target's body cannot be read");
			return;
		}
		g_noBodySince = 0;

		// a menu is up: hold the lock, turn nothing, mark nothing
		auto* im = RE::InterfaceManager::GetInstance(false, false);
		if (!im || im->menuMode != 1) {
			marker::Lock(nullptr);
			return;
		}

		// the right stick flicked: the next target to that side
		if (s.lockOnStickSwitch) {
			float rx = 0.0f;
			if (game::PadRightX(rx)) {
				if (g_stickArmed && std::abs(rx) >= kFlick) {
					g_stickArmed = false;
					const double fromYaw = std::atan2(at[1] - me[1], at[0] - me[0]) * 180.0 / std::numbers::pi;
					const auto   all = Gather(player, me, cam, Forward(camRot), range);
					if (const auto* next = NextToward(all, fromYaw, rx > 0 ? 1 : -1)) {
						++g_switches;
						Engage(*next, rx > 0 ? "switched right to" : "switched left to");
						body = next->body;
						at = next->at;
						if (!AimPoint(body, aim)) aim = at;
					}
				} else if (std::abs(rx) <= kRearm) {
					g_stickArmed = true;
				}
			}
		}

		// where to look: the yaw along the line from the player's body to the target's skeleton, the pitch from the camera to it
		const double goalYaw = std::atan2(aim[1] - me[1], aim[0] - me[0]) * 180.0 / std::numbers::pi;
		const double dz = aim[2] - cam[2];
		const double goalPitch = std::clamp(std::atan2(dz, std::hypot(aim[0] - cam[0], aim[1] - cam[1])) * 180.0 / std::numbers::pi - s.lockOnLookDown, kPitchMin, kPitchMax);

		// from our own last rotation (the stick's turning this frame is not followed); on the first frame the camera's
		std::array<double, 3> cur = g_aimed ? g_lastRot : camRot;
		const double k = s.lockOnTurnTime <= 0.0f ? 1.0 : 1.0 - std::exp(-std::max(0.0, a_in.dt) / s.lockOnTurnTime);
		const std::array<double, 3> rot{ Normal(cur[0] + Normal(goalPitch - cur[0]) * k), Normal(cur[1] + Normal(goalYaw - cur[1]) * k), 0.0 };
		SetVec(a_in.ctrl, Find(a_in.ctrl, "ControlRotation"), rot);
		g_lastRot = rot;
		g_aimed = true;

		marker::Lock(s.lockOnMarker ? actor : nullptr);
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
			{ "ultimate_combat_lock_on_switched_on", g_ucrOn.load() } };
	}
}
