#include "Game.h"

#include "Reflect.h"
#include "Settings.h"

#include <MinHook.h>

namespace game
{
	namespace
	{
		using namespace reflect;
		using Clock = std::chrono::steady_clock;

		constexpr std::size_t kProcessEventSlot = 0x4D;   // UObject::ProcessEvent (CommonLibOB64 UObject.h)
		constexpr const wchar_t* kPlayerClassPath = L"/Game/Dev/PlayerBlueprints/BP_OblivionPlayerCharacter.BP_OblivionPlayerCharacter_C";
		constexpr const wchar_t* kControllerClassPath = L"/Script/Altar.VEnhancedAltarPlayerController";
		constexpr const wchar_t* kSpellCastClassPath = L"/Script/Altar.VSpellCastSingleAnimInstance";

		using ProcessEvent_t = void (*)(UE::UObject*, UE::UFunction*, void*);
		std::atomic<ProcessEvent_t> g_original{ nullptr };
		std::string g_processEventText;

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

			const bool firstPerson = o.fpArm && GetBool(o.fpArm, Find(o.fpArm, "bVisible"));
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
					if (s.cameraStyle == 1) {
						attacking = CallBool(a_pawn, g_fnIsAttacking).value_or(false);
						locked = attacking || g_clock < g_lockUntil || (s.faceWhileHeld && g_held);
					} else {
						locked = combat;
					}
					// free: the camera orbits and the body turns to where it walks; locked: the body turns to the camera
					WriteSwitches(o, locked ? std::array<bool, 4>{ true, false, false, true } : std::array<bool, 4>{ true, false, true, false });
					mode = locked ? "free camera - facing the camera" : "free camera";
				}
				g_wroteSwitches = true;
			}
			ApplyShoulder(o, s.enabled);

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

		void HookedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			static double s_lastTickHandled = 0.0;
			if (a_fn && a_obj && UE::IsInGameThread()) {
				Resolve();
				if (a_fn == g_fnTick && a_obj->GetClass() == g_playerClass) {
					// once per frame: an override's Super call can reach ProcessEvent again with the same pair
					const double now = Seconds();
					if (now - s_lastTickHandled > 0.002) {
						s_lastTickHandled = now;
						OnPlayerTick(a_obj);
					}
				} else if (a_fn == g_fnBlockPressed || a_fn == g_fnBlockReleased || a_fn == g_fnAttackPressed || a_fn == g_fnAttackReleased ||
						   std::ranges::find(g_fnCast, a_fn) != g_fnCast.end()) {
					if (a_fn) OnAction(a_fn);
				}
			}
			if (auto* orig = g_original.load(std::memory_order_acquire)) {
				orig(a_obj, a_fn, a_params);
			}
		}

		bool InsideGameImage(const void* a_p)
		{
			const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const auto p = reinterpret_cast<std::uintptr_t>(a_p);
			return p >= base && p < base + nt->OptionalHeader.SizeOfImage;
		}

		bool TryInstall()
		{
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr || arr->GetObjectArrayNum() < 1000) return false;
			// the ProcessEvent body the first objects' vtables share (a majority, and inside the game's image)
			std::unordered_map<void*, int> votes;
			arr->LockInternalArray();
			const std::int32_t n = std::min(arr->GetObjectArrayNum(), 256);
			for (std::int32_t i = 0; i < n; ++i) {
				auto* item = arr->IndexToObject(i);
				if (!item || !item->object) continue;
				void** vt = *reinterpret_cast<void***>(item->object);
				if (vt && vt[kProcessEventSlot]) ++votes[vt[kProcessEventSlot]];
			}
			arr->UnlockInternalArray();
			void* target = nullptr;
			int best = 0;
			for (const auto& [p, c] : votes) if (c > best) { best = c; target = p; }
			if (!target || !InsideGameImage(target)) {
				SetProblem("ProcessEvent: no body inside the game image yet");
				return false;
			}
			const MH_STATUS init = MH_Initialize();   // this DLL's own MinHook; other plugins have theirs
			if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
				SetProblem(std::format("MinHook would not start ({})", static_cast<int>(init)));
				return true;   // do not retry forever
			}
			void* original = nullptr;
			const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&HookedProcessEvent), &original);
			if (created != MH_OK) {
				SetProblem(std::format("MinHook refused ProcessEvent at {:p} ({})", target, static_cast<int>(created)));
				return true;
			}
			g_original.store(reinterpret_cast<ProcessEvent_t>(original), std::memory_order_release);   // BEFORE enabling
			if (MH_EnableHook(target) != MH_OK) {
				SetProblem(std::format("MinHook could not enable the ProcessEvent hook at {:p}", target));
				return true;
			}
			g_processEventText = std::format("{:p} ({} of {} objects agree)", target, best, n);
			logger::info("ProcessEvent hooked at {}", g_processEventText);
			{
				std::scoped_lock l(g_snapLock);
				g_snap.hookInstalled = true;
				g_snap.processEvent = g_processEventText;
				g_snap.problem.clear();
			}
			g_installed = true;
			return true;
		}
	}

	void Install()
	{
		std::thread([] {
			for (int i = 0; i < 1200; ++i) {   // ten minutes at most
				if (TryInstall()) return;
				std::this_thread::sleep_for(500ms);
			}
			SetProblem("the object array never filled - CCM is not running");
		}).detach();
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
