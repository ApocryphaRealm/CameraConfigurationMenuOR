#include "Game.h"

#include "Compass.h"
#include "PEHook.h"
#include "Reflect.h"
#include "Settings.h"
#include "Ue.h"

namespace game
{
	namespace
	{
		using namespace reflect;
		using Clock = std::chrono::steady_clock;

		constexpr const wchar_t* kPlayerClassPath = L"/Game/Dev/PlayerBlueprints/BP_OblivionPlayerCharacter.BP_OblivionPlayerCharacter_C";
		constexpr const wchar_t* kControllerClassPath = L"/Script/Altar.VEnhancedAltarPlayerController";
		constexpr const wchar_t* kSpellCastClassPath = L"/Script/Altar.VSpellCastSingleAnimInstance";

		// the classes watched through their ProcessEvent vtable slot (pe::Watch - one slot swapped per vtable, never the shared
		// body, so UE4SS's own ProcessEvent hook is left as it is)
		bool                     g_watchPlayer = false;
		UE::UClass*              g_watchedController = nullptr;
		std::vector<UE::UClass*> g_watchedCasts;
		ULONGLONG                g_nextWatch = 0, g_nextCastScan = 0;

		// ---- resolved on the game thread, lazily (rule 17: retried until found) ----
		UE::UClass*    g_playerClass = nullptr;
		UE::UFunction* g_fnTick = nullptr;
		UE::UFunction* g_fnIsAttacking = nullptr;
		UE::UFunction* g_fnBlockPressed = nullptr;
		UE::UFunction* g_fnBlockReleased = nullptr;
		UE::UFunction* g_fnAttackPressed = nullptr;
		UE::UFunction* g_fnAttackReleased = nullptr;
		std::array<UE::UFunction*, 4> g_fnCast{};
		Clock::time_point g_nextResolve{};

		// ---- per-tick state (game thread only) ----
		Clock::time_point g_lastTick{};
		double g_clock = 0.0;            // seconds of game ticks - stops when the ticks stop
		double g_lockUntil = 0.0;
		bool   g_held = false;
		bool   g_wroteSwitches = false;
		bool   g_vanillaRecorded = false;
		std::array<bool, 4> g_vanilla{};
		bool   g_shoulderLeft = false;
		bool   g_socketWritten = false;
		std::array<double, 3> g_socketBase{}, g_socketLast{};
		std::string g_lastTag;
		std::vector<std::string> g_tagsSeen;
		std::uint64_t g_ticks = 0;
		std::uint32_t g_blockEvents = 0, g_attackEvents = 0, g_castEvents = 0;
		std::array<bool, 256> g_keyDown{};
		Clock::time_point g_lastSnapshot{};
		double g_rateWindowStart = 0.0;
		std::uint64_t g_rateWindowTicks = 0;
		double g_ticksPerSecond = 0.0;

		// ---- shared with the page / tool threads ----
		std::mutex         g_snapLock;
		Snapshot           g_snap;
		std::mutex         g_queueLock;
		std::vector<Action> g_queue;
		std::atomic<std::int64_t> g_pageDrawnAt{ 0 };
		std::atomic<bool>  g_installed{ false };

		std::string Hex(const void* a_p) { return a_p ? std::format("{:p}", a_p) : std::string{}; }
		double Seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

		void SetProblem(std::string a_why)
		{
			std::scoped_lock l(g_snapLock);
			if (g_snap.problem != a_why) {
				logger::warn("{}", a_why);
				g_snap.problem = std::move(a_why);
			}
		}

		void Resolve()
		{
			if (g_playerClass && g_fnTick) return;
			if (Clock::now() < g_nextResolve) return;
			g_nextResolve = Clock::now() + 250ms;
			if (!g_playerClass) {
				g_playerClass = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kPlayerClassPath);
				if (!g_playerClass) return;   // main menu - not loaded yet
				logger::info("player class found at {:p}", static_cast<void*>(g_playerClass));
			}
			g_fnTick = FindFunction(g_playerClass, L"ReceiveTick");
			g_fnIsAttacking = FindFunction(g_playerClass, L"IsAttacking");
			if (auto* c = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kControllerClassPath)) {
				g_fnBlockPressed = FindFunction(c, L"BlockInput_Pressed");
				g_fnBlockReleased = FindFunction(c, L"BlockInput_Released");
				g_fnAttackPressed = FindFunction(c, L"OnAttackRequestPressed");
				g_fnAttackReleased = FindFunction(c, L"OnAttackRequestReleased");
			}
			if (auto* c = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, kSpellCastClassPath)) {
				// "Targe" is the game's own spelling (Player Camera hooks the same four)
				const wchar_t* names[4] = { L"OnCastTargeRightEnter", L"OnCastTargetLeftEnter", L"OnCastTouchLeftEnter", L"OnCastTouchRightEnter" };
				for (std::size_t i = 0; i < 4; ++i) g_fnCast[i] = FindFunction(c, names[i]);
			}
			logger::info("functions: ReceiveTick {:p}, IsAttacking {:p}, block {:p}/{:p}, attack {:p}/{:p}, casts {:p} {:p} {:p} {:p}",
				static_cast<void*>(g_fnTick), static_cast<void*>(g_fnIsAttacking), static_cast<void*>(g_fnBlockPressed), static_cast<void*>(g_fnBlockReleased),
				static_cast<void*>(g_fnAttackPressed), static_cast<void*>(g_fnAttackReleased), static_cast<void*>(g_fnCast[0]), static_cast<void*>(g_fnCast[1]),
				static_cast<void*>(g_fnCast[2]), static_cast<void*>(g_fnCast[3]));
			if (!g_fnTick) SetProblem("ReceiveTick not found on the player class - CCM cannot run");
		}

		bool GameInFront()
		{
			DWORD pid = 0;
			const HWND fg = ::GetForegroundWindow();
			return fg && ::GetWindowThreadProcessId(fg, &pid) && pid == ::GetCurrentProcessId();
		}

		bool KeyPressedEdge(std::int32_t a_scan)
		{
			if (a_scan <= 0 || a_scan > 255) return false;
			const UINT vk = ::MapVirtualKeyW(static_cast<UINT>(a_scan), MAPVK_VSC_TO_VK_EX);
			const bool down = vk && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
			const bool edge = down && !g_keyDown[static_cast<std::size_t>(a_scan)];
			g_keyDown[static_cast<std::size_t>(a_scan)] = down;
			return edge;
		}

		void ReadKeys(const settings::Values& a_s)
		{
			// Quiet while the game is not in front or CCM's own page drew in the last 250 ms (AMF's menu is open).
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
			const bool quiet = !GameInFront() || now - g_pageDrawnAt.load(std::memory_order_relaxed) < 250;
			const bool swap = KeyPressedEdge(a_s.shoulderSwapKey);
			const bool cycle = KeyPressedEdge(a_s.cycleStyleKey);
			const bool toggle = KeyPressedEdge(a_s.toggleKey);
			if (quiet) return;
			if (swap) Queue(Action::kShoulderSwap);
			if (cycle) Queue(Action::kCycleStyle);
			if (toggle) Queue(Action::kToggle);
		}

		struct Objects
		{
			UE::UObject* pawn = nullptr;
			UE::UObject* ctrl = nullptr;
			UE::UObject* arm = nullptr;
			UE::UObject* fpArm = nullptr;
			UE::UObject* move = nullptr;
			UE::UObject* mgr = nullptr;
		};

		std::array<bool, 4> ReadSwitches(const Objects& o)
		{
			return { GetBool(o.arm, Find(o.arm, "bUsePawnControlRotation")), GetBool(o.pawn, Find(o.pawn, "bUseControllerRotationYaw")),
				GetBool(o.move, Find(o.move, "bOrientRotationToMovement")), GetBool(o.move, Find(o.move, "bUseControllerDesiredRotation")) };
		}

		void WriteSwitches(const Objects& o, const std::array<bool, 4>& a_v)
		{
			SetBool(o.arm, Find(o.arm, "bUsePawnControlRotation"), a_v[0]);
			SetBool(o.pawn, Find(o.pawn, "bUseControllerRotationYaw"), a_v[1]);
			SetBool(o.move, Find(o.move, "bOrientRotationToMovement"), a_v[2]);
			SetBool(o.move, Find(o.move, "bUseControllerDesiredRotation"), a_v[3]);
		}

		void RestoreVanilla(const Objects& o, const char* a_why)
		{
			if (g_wroteSwitches && g_vanillaRecorded && o.arm && o.move) {
				WriteSwitches(o, g_vanilla);
				logger::info("free camera off ({}): the game's switches put back", a_why);
			}
			g_wroteSwitches = false;
			g_vanillaRecorded = false;
			g_lockUntil = 0.0;
		}

		bool Near(const std::array<double, 3>& a, const std::array<double, 3>& b)
		{
			return std::abs(a[0] - b[0]) < 0.01 && std::abs(a[1] - b[1]) < 0.01 && std::abs(a[2] - b[2]) < 0.01;
		}

		// Shoulder swap through the manager's setting data: the game's socket offset is the base, re-based whenever the
		// game writes a value CCM did not (a state change, a zoom), and CCM writes the mirrored Y on top.
		void ApplyShoulder(const Objects& o, bool a_enabled)
		{
			if (!o.mgr) return;
			const auto& p = Find(o.mgr, "CurrentCameraSettingData.DesiredSocketOffset");
			if (!p.Ok()) return;
			const auto cur = GetVec(o.mgr, p);
			if (!g_socketWritten || !Near(cur, g_socketLast)) {
				g_socketBase = cur;
			}
			auto target = g_socketBase;
			if (a_enabled && g_shoulderLeft) target[1] = -target[1];
			if (!Near(target, cur)) SetVec(o.mgr, p, target);
			g_socketLast = target;
			g_socketWritten = true;
		}

		void RunActions(const Objects& o, settings::Values& a_s)
		{
			std::vector<Action> todo;
			{
				std::scoped_lock l(g_queueLock);
				todo.swap(g_queue);
			}
			for (const auto a : todo) {
				switch (a) {
				case Action::kShoulderSwap:
					g_shoulderLeft = !g_shoulderLeft;
					logger::info("shoulder swap: camera over the {} shoulder", g_shoulderLeft ? "left" : "right");
					break;
				case Action::kCycleStyle:
					a_s.cameraStyle = a_s.cameraStyle == 1 ? 2 : 1;
					logger::info("camera style -> {}", a_s.cameraStyle == 1 ? "free" : "free, locked with the weapon drawn");
					settings::Save();
					break;
				case Action::kToggle:
					a_s.enabled = !a_s.enabled;
					logger::info("CCM {}", a_s.enabled ? "on" : "off");
					settings::Save();
					break;
				case Action::kUnstick:
					RestoreVanilla(o, "unstick");
					g_shoulderLeft = false;
					g_held = false;
					break;
				}
			}
		}

		// Ultimate Combat Redux's lock-on, from the state file it writes beside its settings on engage and release
		// ("MadConfigs\Ultimate Combat.lockon", locked=1 / locked=0), read at most every 100 ms
		bool UltimateCombatLockedOn()
		{
			static bool      s_locked = false;
			static ULONGLONG s_nextRead = 0;
			const ULONGLONG  now = GetTickCount64();
			if (now < s_nextRead) return s_locked;
			s_nextRead = now + 100;
			static const auto path = settings::PluginFolder().parent_path().parent_path() / L"MadConfigs" / L"Ultimate Combat.lockon";
			bool locked = false;
			std::FILE* f = nullptr;
			if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
				char buf[32]{};
				const auto n = std::fread(buf, 1, sizeof(buf) - 1, f);
				std::fclose(f);
				locked = n > 0 && std::string_view(buf, n).find("locked=1") != std::string_view::npos;
			}
			if (locked != s_locked) {
				s_locked = locked;
				logger::info("Ultimate Combat {} - {}", locked ? "locked on" : "lock released", locked ? "the free camera stands down, the body faces the camera" : "the free camera again");
			}
			return s_locked;
		}

		void OnPlayerTick(UE::UObject* a_pawn)
		{
			const auto t0 = Clock::now();
			const double dt = g_lastTick.time_since_epoch().count() == 0 ? 0.0 : std::min(0.1, std::chrono::duration<double>(t0 - g_lastTick).count());
			g_lastTick = t0;
			g_clock += dt;
			++g_ticks;

			auto& s = settings::Get();
			Objects o;
			o.pawn = a_pawn;
			o.arm = GetObject(a_pawn, Find(a_pawn, "ThirdPersonCameraSpringArmComponent"));
			o.fpArm = GetObject(a_pawn, Find(a_pawn, "FirstPersonCameraSpringArmComponent"));
			o.move = GetObject(a_pawn, Find(a_pawn, "CharacterMovement"));
			o.ctrl = GetObject(a_pawn, Find(a_pawn, "Controller"));
			o.mgr = o.ctrl ? GetObject(o.ctrl, Find(o.ctrl, "PlayerCameraManager")) : nullptr;

			ReadKeys(s);
			RunActions(o, s);

			std::string tag;
			if (o.mgr) {
				tag = GetName(o.mgr, Find(o.mgr, "CameraTags.TagName"));
				if (tag != g_lastTag) {
					logger::debug("camera state {} -> {}", g_lastTag.empty() ? "(none)" : g_lastTag, tag.empty() ? "(none)" : tag);
					if (!tag.empty() && std::ranges::find(g_tagsSeen, tag) == g_tagsSeen.end()) {
						g_tagsSeen.push_back(tag);
						logger::info("camera state seen for the first time: {}", tag);
					}
					g_lastTag = tag;
				}
			}

			// first person from the Gamebryo side (PlayerCharacter::is3rdPerson - proven in Better Third-Person Selection), held
			// 150 ms before a switch counts: it reads false for a single frame now and then in third person. The first-person
			// arm's bVisible (the old test) is not what the game's own view uses (UCR asks IsVisible / POV).
			static bool      s_firstPerson = false;
			static ULONGLONG s_differentSince = 0;
			if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
				const bool raw = !pc->is3rdPerson;
				const ULONGLONG nowMs = GetTickCount64();
				if (raw != s_firstPerson) {
					if (!s_differentSince) {
						s_differentSince = nowMs;
					} else if (nowMs - s_differentSince >= 150) {
						s_firstPerson = raw;
						s_differentSince = 0;
						logger::debug("view: {}", raw ? "first person" : "third person");
					}
				} else {
					s_differentSince = 0;
				}
			}
			const bool firstPerson = s_firstPerson;
			const bool combat = GetBool(a_pawn, Find(a_pawn, "bInCombatStance"));
			bool attacking = false;
			const char* mode = "vanilla";
			bool locked = false;
			const bool styleFree = s.enabled && (s.cameraStyle == 1 || s.cameraStyle == 2);

			if (!o.arm || !o.move) {
				mode = "waiting for the camera arm";
			} else if (!styleFree) {
				RestoreVanilla(o, s.enabled ? "style is vanilla" : "CCM is off");
				mode = s.enabled ? "vanilla style" : "off";
			} else {
				if (!g_vanillaRecorded) {
					g_vanilla = ReadSwitches(o);
					g_vanillaRecorded = true;
					logger::info("the game's switches recorded: pawn-control rotation {}, controller yaw {}, orient to movement {}, desired rotation {}",
						g_vanilla[0], g_vanilla[1], g_vanilla[2], g_vanilla[3]);
				}
				if (firstPerson) {
					WriteSwitches(o, { false, true, false, true });   // Player Camera's first-person values
					mode = "first person";
				} else {
					const bool lockedOn = s.faceWhileLockedOn && UltimateCombatLockedOn();
					if (s.cameraStyle == 1) {
						attacking = CallBool(a_pawn, g_fnIsAttacking).value_or(false);
						locked = lockedOn || attacking || g_clock < g_lockUntil || (s.faceWhileHeld && g_held);
					} else {
						locked = lockedOn || combat;
					}
					// free: the camera orbits and the body turns to where it walks; locked: the body turns to the camera
					WriteSwitches(o, locked ? std::array<bool, 4>{ true, false, false, true } : std::array<bool, 4>{ true, false, true, false });
					mode = locked ? "free camera - facing the camera" : "free camera";
				}
				g_wroteSwitches = true;
			}
			ApplyShoulder(o, s.enabled);
			// the compass follows the camera while the free camera is on in third person (UCR's CompassBridge does the same)
			compass::Update(o.ctrl, s.enabled && styleFree && !firstPerson && s.compassFollowsCamera && o.arm && o.move);

			// the tick rate over one-second windows, for the Status page
			++g_rateWindowTicks;
			if (g_clock - g_rateWindowStart >= 1.0) {
				g_ticksPerSecond = static_cast<double>(g_rateWindowTicks) / (g_clock - g_rateWindowStart);
				g_rateWindowStart = g_clock;
				g_rateWindowTicks = 0;
			}

			const double micros = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
			if (Clock::now() - g_lastSnapshot >= 100ms) {
				g_lastSnapshot = Clock::now();
				std::scoped_lock l(g_snapLock);
				g_snap.ticks = g_ticks;
				g_snap.tickMicros = micros;
				g_snap.ticksPerSecond = g_ticksPerSecond;
				g_snap.pawn = Hex(o.pawn);
				g_snap.controller = Hex(o.ctrl);
				g_snap.arm = Hex(o.arm);
				g_snap.fpArm = Hex(o.fpArm);
				g_snap.movement = Hex(o.move);
				g_snap.cameraManager = Hex(o.mgr);
				g_snap.cameraTag = tag;
				g_snap.tagsSeen = g_tagsSeen;
				g_snap.firstPerson = firstPerson;
				g_snap.combatStance = combat;
				g_snap.attacking = attacking;
				g_snap.active = g_wroteSwitches;
				g_snap.locked = locked;
				g_snap.lockRemaining = std::max(0.0, g_lockUntil - g_clock);
				g_snap.mode = mode;
				if (o.arm && o.move) g_snap.switches = ReadSwitches(o);
				g_snap.vanilla = g_vanilla;
				g_snap.vanillaRecorded = g_vanillaRecorded;
				g_snap.shoulderLeft = g_shoulderLeft;
				g_snap.socketBase = g_socketBase;
				if (o.mgr) {
					g_snap.socketNow = GetVec(o.mgr, Find(o.mgr, "CurrentCameraSettingData.DesiredSocketOffset"));
					g_snap.desiredArmLength = GetFloat(o.mgr, Find(o.mgr, "CurrentCameraSettingData.DesiredArmLength"));
					g_snap.fov = GetFloat(o.mgr, Find(o.mgr, "CurrentCameraSettingData.DesiredOverrideFieldOfView"));
				}
				if (o.arm) g_snap.armLengthNow = GetFloat(o.arm, Find(o.arm, "TargetArmLength"));
				g_snap.blockEvents = g_blockEvents;
				g_snap.attackEvents = g_attackEvents;
				g_snap.castEvents = g_castEvents;
			}
		}

		void OnAction(UE::UFunction* a_fn)
		{
			const auto& s = settings::Get();
			if (a_fn == g_fnBlockPressed) {
				++g_blockEvents;
				g_held = true;
				g_lockUntil = std::max(g_lockUntil, g_clock + s.blockTurnSeconds);
			} else if (a_fn == g_fnAttackPressed) {
				++g_attackEvents;
				g_held = true;
				g_lockUntil = std::max(g_lockUntil, g_clock + s.attackTurnSeconds);
			} else if (a_fn == g_fnBlockReleased || a_fn == g_fnAttackReleased) {
				g_held = false;
				// Player Camera frees the body the moment block or attack is released; only a spell outlasts its button
				if (!s.faceWhileHeld) g_lockUntil = 0.0;
			} else {
				++g_castEvents;
				g_lockUntil = std::max(g_lockUntil, g_clock + s.spellTurnSeconds);
			}
		}

		// the player's class: ReceiveTick is CCM's per-frame camera point (probe P1)
		void OnPlayerEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			static double s_lastTickHandled = 0.0;
			if (a_fn && a_obj && a_fn == g_fnTick) {
				// once per frame: an override's Super call can reach ProcessEvent again with the same pair
				const double now = Seconds();
				if (now - s_lastTickHandled > 0.002) {
					s_lastTickHandled = now;
					OnPlayerTick(a_obj);
				}
			}
		}

		// the controller's class: block and attack presses and releases
		void OnControllerEvent(UE::UObject*, UE::UFunction* a_fn, void*)
		{
			if (a_fn && (a_fn == g_fnBlockPressed || a_fn == g_fnBlockReleased || a_fn == g_fnAttackPressed || a_fn == g_fnAttackReleased)) {
				OnAction(a_fn);
			}
		}

		// the spell-cast animation classes: the four OnCast*Enter
		void OnCastEvent(UE::UObject*, UE::UFunction* a_fn, void*)
		{
			if (a_fn && std::ranges::find(g_fnCast, a_fn) != g_fnCast.end()) {
				OnAction(a_fn);
			}
		}

		void PublishWatches()
		{
			std::string text = g_watchPlayer ? "player" : "player (waiting)";
			text += g_watchedController ? ", controller" : ", controller (waiting)";
			text += std::format(", {} spell-cast class(es)", g_watchedCasts.size());
			std::scoped_lock l(g_snapLock);
			g_snap.hookInstalled = g_watchPlayer;
			g_snap.processEvent = "vtable watches: " + text;
			if (g_watchPlayer && g_snap.problem.starts_with("waiting")) g_snap.problem.clear();
		}

		// The watches, made on the game thread and retried until each class exists (rule 17): the player's class and the live
		// controller's class every 2 s until watched; every live class deriving from VSpellCastSingleAnimInstance every 10 s (a
		// class appears when the first spell of its kind is cast).
		void MakeWatches()
		{
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextWatch) return;
			g_nextWatch = now + 2000;
			if (!ue::SelfCheck()) return;
			Resolve();
			compass::Install();   // the native compass getter, swapped once it exists
			bool changed = false;
			if (!g_watchPlayer && g_playerClass && g_fnTick) {
				// AFTER the Blueprint tick: it re-applies the stance flags, so a write made before it is undone the same frame
				// (round 2 - the free camera did nothing; UCR writes in a post hook on the same event)
				g_watchPlayer = pe::Watch(g_playerClass, &OnPlayerEvent, true);
				changed |= g_watchPlayer;
				if (g_watchPlayer) logger::info("watching the player's class (ReceiveTick, after its body) through its ProcessEvent vtable slot");
			}
			if (!g_watchedController && (g_fnBlockPressed || g_fnAttackPressed)) {
				if (auto* pc = ue::FirstOf(ue::Class(L"/Script/Engine.PlayerController")); pc && pc->GetClass()) {
					if (pe::Watch(pc->GetClass(), &OnControllerEvent)) {
						g_watchedController = pc->GetClass();
						changed = true;
						logger::info("watching the controller's class {} (block and attack)", ue::NameOf(pc->GetClass()));
					}
				}
			}
			if (now >= g_nextCastScan && std::ranges::any_of(g_fnCast, [](auto* f) { return f != nullptr; })) {
				g_nextCastScan = now + 10000;
				static auto* base = ue::Class(L"/Script/Altar.VSpellCastSingleAnimInstance");
				auto* arr = UE::FUObjectArray::GetSingleton();
				std::vector<UE::UClass*> found;
				if (base && arr) {
					arr->LockInternalArray();
					const std::int32_t n = arr->GetObjectArrayNum();
					for (std::int32_t i = 0; i < n; ++i) {
						auto* item = arr->IndexToObject(i);
						auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
						auto* cls = o ? o->GetClass() : nullptr;
						if (cls && cls->IsChildOf(base) && o != cls->GetDefaultObject(false) && std::ranges::find(g_watchedCasts, cls) == g_watchedCasts.end() &&
							std::ranges::find(found, cls) == found.end()) {
							found.push_back(cls);
						}
					}
					arr->UnlockInternalArray();
				}
				for (auto* cls : found) {
					if (pe::Watch(cls, &OnCastEvent)) {
						g_watchedCasts.push_back(cls);
						changed = true;
						logger::info("watching the spell-cast class {} (the four casts)", ue::NameOf(cls));
					}
				}
			}
			if (changed || !g_watchPlayer) PublishWatches();
		}
	}

	void FrameTick()
	{
		MakeWatches();
	}

	Snapshot Status()
	{
		std::scoped_lock l(g_snapLock);
		return g_snap;
	}

	void Queue(Action a_action)
	{
		std::scoped_lock l(g_queueLock);
		g_queue.push_back(a_action);
	}

	void NotePageDrawn()
	{
		g_pageDrawnAt.store(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
	}
}
