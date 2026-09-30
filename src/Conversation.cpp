#include "Conversation.h"

#include "Aim.h"
#include "Framing.h"
#include "Reflect.h"
#include "Settings.h"
#include "Ue.h"

namespace conversation
{
	namespace
	{
		using reflect::Find;
		using reflect::GetBool;
		using reflect::GetObject;
		using reflect::SetBool;
		using reflect::SetVec;

		std::string Narrow(const std::wstring& a_w)
		{
			std::string out;
			for (const wchar_t c : a_w) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
			return out;
		}

		bool           g_in = false;
		bool           g_switched = false;
		bool           g_forced = false;           // the switch did not hold in the conversation: ForceAndLockPOV was used
		bool           g_thirdForced = false;      // the conversation is held in third person (Move the conversation camera)
		int            g_checkIn = 0;              // ticks until the switch is read back
		std::uint8_t   g_previousPov = 1;          // EVPlayerPOVType: 0 first person, 1 third close, 2 third far
		// The view the player had in GAMEPLAY, followed every tick outside a conversation. The game's own dialogue camera
		// reports POV 0 (first person) for the whole conversation while it frames the speaker itself (log 2026-09-30
		// 00:48:52, "already in first person" in third person) - so a conversation's view is never read from POV.
		std::uint8_t   g_gameplayPov = 1;
		RE::TESFormID  g_lastTalkable = 0;         // the last person or creature the player could activate, in gameplay
		ULONGLONG      g_lastTalkableAt = 0;
		RE::TESFormID  g_speaker = 0;
		std::string    g_speakerName;
		std::array<double, 3> g_lastAim{};
		bool           g_aimed = false;
		int            g_overridden = 0;           // ticks the game's own aim replaced ours (read back the next tick)

		// the speaker's Unreal body, found once a conversation (its head socket is the aim point, as Ultimate Combat aims)
		ue::Handle   g_pawn, g_mesh;
		bool         g_pawnSearched = false;
		std::string  g_pawnMatch = "not looked for";
		std::wstring g_socket;                     // the socket in use, empty = none (the Oblivion head position is used)
		ULONGLONG    g_lastStep = 0;

		// the player's third-person arm, held for the lock the way Ultimate Combat's lock-on freezes it (LockOn.lua
		// freeze_arm / restore_arm): no inherited rotation, absolute rotation, and put back when the lock ends
		ue::Handle g_arm;
		bool       g_armFrozen = false;
		std::array<bool, 4> g_armSaved{};         // bInheritPitch, bInheritYaw, bInheritRoll, bAbsoluteRotation

		std::mutex  g_lock;
		std::string g_status = "no conversation yet";
		std::string g_shownSpeaker, g_shownBody, g_shownSocket;   // copies for State(), which other threads call

		void Publish()
		{
			std::scoped_lock l(g_lock);
			g_shownSpeaker = g_speakerName;
			g_shownBody = g_pawnMatch;
			g_shownSocket = Narrow(g_socket);
		}

		// Ultimate Combat's lock-on rates (LockOn.lua, Kramer7046 - modification allowed with credit): each tick a fifth of
		// the way to the goal, at most 300 degrees a second of yaw and 180 of pitch; pitch held within 75 degrees
		constexpr double kStepShare = 0.20;
		constexpr double kYawRate = 300.0, kPitchRate = 180.0;
		constexpr double kPitchLimit = 75.0;

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) logger::info("conversation: {}", a_s);
			g_status = std::move(a_s);
		}

		// the speaker's reference. NOT LookupByID<TESObjectREFR>: As<T>() tests the exact form type, and a person is an
		// ACHR (Actor), so it came back null every time - no name, no body, and the lock never ran (log 2026-09-29 23:59:
		// "in a conversation with (no name)", "body no speaker")
		RE::TESObjectREFR* RefOf(RE::TESFormID a_id)
		{
			auto* form = a_id ? RE::TESForm::LookupByID(a_id) : nullptr;
			if (!form) return nullptr;
			switch (form->GetFormType()) {
			case RE::FormType::Reference:
			case RE::FormType::ActorCharacter:
			case RE::FormType::ActorCreature:
				return static_cast<RE::TESObjectREFR*>(form);
			default:
				return nullptr;
			}
		}

		bool Talkable(RE::TESObjectREFR* a_ref)
		{
			auto* base = a_ref ? a_ref->data.objectReference : nullptr;
			const auto type = base ? base->GetFormType() : RE::FormType::None;
			return type == RE::FormType::NPC || type == RE::FormType::Creature;
		}

		std::uint8_t Pov(UE::UObject* a_ctrl)
		{
			auto* cls = a_ctrl ? a_ctrl->GetClass() : nullptr;
			const auto off = cls ? ue::Offset(cls, "POV") : -1;
			auto* p = off >= 0 ? ue::At<std::uint8_t>(a_ctrl, off) : nullptr;
			return p ? *p : 1;
		}

		bool ForcePov(UE::UObject* a_ctrl, std::uint8_t a_pov)
		{
			ue::Call c(a_ctrl, L"ForceAndLockPOV");
			if (!c) return false;
			c.Set("TargetPOV", a_pov);
			return c.Run();
		}

		bool UnlockPov(UE::UObject* a_ctrl)
		{
			ue::Call c(a_ctrl, L"UnlockAndRestorePOV");
			return c && c.Run();
		}

		bool SwitchPov(UE::UObject* a_ctrl, std::uint8_t a_pov)
		{
			ue::Call c(a_ctrl, L"SwitchPOV");
			if (!c) return false;
			c.Set("TargetPOV", a_pov);
			c.Set("bSetToNewDefaultState", false);
			return c.Run();
		}

		double Normal(double a)
		{
			a = std::fmod(a, 360.0);
			if (a > 180.0) a -= 360.0;
			if (a < -180.0) a += 360.0;
			return a;
		}

		double Shortest(double a_from, double a_to) { return Normal(a_to - a_from); }

		bool SocketExists(UE::UObject* a_mesh, const wchar_t* a_name)
		{
			ue::Call c(a_mesh, L"DoesSocketExist");
			if (!c) return false;
			c.Set("InSocketName", UE::FName(a_name, UE::EFindName::Add));
			c.Run();
			return c.Get<bool>("ReturnValue");
		}

		// The speaker's pawn: the paired pawn whose reference component carries the speaker's form ID, or - when none does -
		// the pawn nearest the speaker's own position (within 3 m). Once a conversation: the object array is scanned whole.
		void FindSpeakerPawn(RE::TESObjectREFR* a_ref)
		{
			g_pawnSearched = true;
			g_pawn = {};
			g_mesh = {};
			g_socket.clear();
			static auto* pawnClass = ue::Class(L"/Script/Altar.VPairedPawn");
			if (!pawnClass || !a_ref) {
				g_pawnMatch = pawnClass ? "no speaker" : "the paired pawn class is not loaded";
				return;
			}
			// The speaker's own body, through the game's pairing: every reference is an IVPairableItem whose pairing entry holds
			// its Unreal actor (hostItem). The form ID in a pawn's TESRefComponent never matched (every conversation fell to
			// "the nearest body, 88 cm from the speaker" - often the PLAYER'S own body, so the lock aimed at the player's
			// head and the owner saw his own body cut across the view up close, 2026-09-30).
			UE::UObject* paired = nullptr;
			if (auto* entry = static_cast<RE::IVPairableItem*>(a_ref)->pairingEntry; entry && entry->isPaired && entry->hostItem && ue::IsLive(entry->hostItem)) {
				paired = entry->hostItem;
			}
			// else the nearest paired pawn within 60 cm of the speaker's own position - never the player's body
			UE::UObject* nearest = nullptr;
			double       best = 60.0;
			if (!paired) {
				UE::UObject* playerPawn = nullptr;
				if (auto* pr = RE::PlayerCharacter::GetSingleton()) {
					if (auto* pe = static_cast<RE::IVPairableItem*>(pr)->pairingEntry; pe && pe->hostItem && ue::IsLive(pe->hostItem)) playerPawn = pe->hostItem;
				}
				UE::FVector want{};
				if (aim::ToUnreal(a_ref->data.location, want)) {
					static ue::Getter location(L"K2_GetActorLocation");
					for (auto* p : ue::AllOf(pawnClass)) {
						if (p == playerPawn) continue;
						std::array<double, 3> at{};
						if (location.Get(p, at)) {
							const double d = std::hypot(at[0] - want.x, at[1] - want.y, at[2] - want.z);
							if (d < best) {
								best = d;
								nearest = p;
							}
						}
					}
				}
			}
			auto* pawn = paired ? paired : nearest;
			g_pawnMatch = paired ? "its own (the game's pairing)" : nearest ? std::format("the nearest body ({:.0f} cm from the speaker)", best) : "no body found - the Oblivion head position is used";
			if (!pawn) return;
			g_pawn.Set(pawn);
			auto* mesh = GetObject(pawn, Find(pawn, "MainSkeletalMeshComponent"));
			if (!mesh) {
				g_pawnMatch += ", no skeletal mesh";
				return;
			}
			g_mesh.Set(mesh);
			for (const wchar_t* name : { L"Head_Socket", L"Spine_Socket", L"Root_Socket" }) {
				if (SocketExists(mesh, name)) {
					g_socket = name;
					break;
				}
			}
			g_pawnMatch += g_socket.empty() ? ", no socket" : std::format(", socket {}", Narrow(g_socket));
			Publish();
		}

		// where to look: the speaker's head socket (Unreal's world), else the Oblivion head position converted
		bool AimPoint(std::array<double, 3>& a_out)
		{
			auto* mesh = g_socket.empty() ? nullptr : g_mesh.Get();
			if (mesh) {
				ue::Call c(mesh, L"GetSocketLocation");
				if (c) {
					c.Set("InSocketName", UE::FName(g_socket.c_str(), UE::EFindName::Add));
					c.Run();
					a_out = c.Get<std::array<double, 3>>("ReturnValue");
					if (a_out[0] != 0.0 || a_out[1] != 0.0 || a_out[2] != 0.0) return true;
				}
			}
			auto* ref = g_speaker ? RefOf(g_speaker) : nullptr;
			if (!ref) return false;
			RE::NiPoint3 head = ref->data.location;
			head.z += 115.0f;   // about eye height on a person (Oblivion units)
			UE::FVector t{};
			if (!aim::ToUnreal(head, t)) return false;
			a_out = { t.x, t.y, t.z };
			return true;
		}

		void FreezeArm(UE::UObject* a_arm)
		{
			if (!a_arm || g_armFrozen) return;
			g_armSaved = { GetBool(a_arm, Find(a_arm, "bInheritPitch")), GetBool(a_arm, Find(a_arm, "bInheritYaw")), GetBool(a_arm, Find(a_arm, "bInheritRoll")),
				GetBool(a_arm, Find(a_arm, "bAbsoluteRotation")) };
			SetBool(a_arm, Find(a_arm, "bInheritPitch"), false);
			SetBool(a_arm, Find(a_arm, "bInheritYaw"), false);
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

		// Ultimate Combat's lock-on, applied to the speaker (the owner, 2026-09-29: "reference how Ultimate Combat locks onto
		// a target and just apply that to the conversation offset camera"): the look-at rotation from the camera's own
		// place to the speaker's head socket, stepped toward at its rates, written to the controller's ControlRotation and
		// to the frozen third-person arm's RelativeRotation - the two writes its tracking tick makes.
		void Aim(UE::UObject* a_ctrl, UE::UObject* a_mgr, UE::UObject* a_arm)
		{
			if (!a_ctrl || !a_mgr || !a_arm) return;
			std::array<double, 3> target{}, cam{};
			static ue::Getter camLocation(L"GetCameraLocation");
			if (!AimPoint(target) || !camLocation.Get(a_mgr, cam)) return;
			const double dx = target[0] - cam[0], dy = target[1] - cam[1], dz = target[2] - cam[2];
			if (std::hypot(dx, dy, dz) < 1.0) return;
			const double goalYaw = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
			const double goalPitch = std::atan2(dz, std::hypot(dx, dy)) * 180.0 / std::numbers::pi;

			// where the view is now: our last write, or on the first tick the camera's own rotation
			std::array<double, 3> cur{};
			static ue::Getter controlRotation(L"GetControlRotation");
			std::array<double, 3> ctrlNow{};
			const bool haveCtrl = controlRotation.Get(a_ctrl, ctrlNow);
			if (g_aimed) {
				if (haveCtrl && (std::abs(Shortest(ctrlNow[0], g_lastAim[0])) > 1.0 || std::abs(Shortest(ctrlNow[1], g_lastAim[1])) > 1.0)) {
					++g_overridden;
				}
				cur = g_lastAim;
			} else {
				static ue::Getter camRotation(L"GetCameraRotation");
				if (!camRotation.Get(a_mgr, cur) && haveCtrl) cur = ctrlNow;
				FreezeArm(a_arm);
			}
			const ULONGLONG now = GetTickCount64();
			const double    dt = std::clamp(g_lastStep ? (now - g_lastStep) / 1000.0 : 1.0 / 60.0, 1.0 / 240.0, 0.05);
			g_lastStep = now;
			const double stepPitch = std::clamp(Shortest(cur[0], goalPitch) * kStepShare, -kPitchRate * dt, kPitchRate * dt);
			const double stepYaw = std::clamp(Shortest(cur[1], goalYaw) * kStepShare, -kYawRate * dt, kYawRate * dt);
			const std::array<double, 3> rot{ std::clamp(Normal(cur[0] + stepPitch), -kPitchLimit, kPitchLimit), Normal(cur[1] + stepYaw), 0.0 };

			SetVec(a_ctrl, Find(a_ctrl, "ControlRotation"), rot);
			SetVec(a_arm, Find(a_arm, "RelativeRotation"), rot);
			g_lastAim = rot;
			g_aimed = true;
		}

		void EndLock()
		{
			RestoreArm();
			g_aimed = false;
			g_lastStep = 0;
		}
	}

	void Tick(UE::UObject* a_controller, UE::UObject* a_cameraManager, UE::UObject* a_arm, const std::string& a_cameraTag)
	{
		const auto& s = settings::Get();
		auto*       im = RE::InterfaceManager::GetInstance(false, false);
		if (!im || !a_controller) return;
		const bool      gameplay = im->menuMode == 1;
		const ULONGLONG now = GetTickCount64();

		if (gameplay && !g_in) {
			g_gameplayPov = Pov(a_controller);
			// the person the player is about to talk to: the game's own activation target (or the crosshair's)
			for (auto* r : { im->activateRef, im->crosshairRef }) {
				if (Talkable(r)) {
					g_lastTalkable = r->GetFormID();
					g_lastTalkableAt = now;
					break;
				}
			}
		}

		const bool dialogue = a_cameraTag.find("Dialogue") != std::string::npos;
		if (!g_in && dialogue) {
			g_in = true;
			g_switched = false;
			g_aimed = false;
			g_overridden = 0;
			g_pawnSearched = false;
			g_pawnMatch = "not looked for";
			g_speaker = now - g_lastTalkableAt < 3000 ? g_lastTalkable : 0;
			auto* ref = g_speaker ? RefOf(g_speaker) : nullptr;
			const char* n = ref && ref->data.objectReference ? RE::TESFullName::GetFullName(ref->data.objectReference) : nullptr;
			g_speakerName = n && *n ? n : (g_speaker ? "(no name)" : "");
			Publish();
			g_forced = false;
			g_checkIn = 0;
			// The game's conversation camera IS first person (dialogue capture 2026-09-30 01:01: POV 0, the first-person arm,
			// arm length 0 in its data). "First person in conversations" therefore switches nothing: it keeps CCM's offsets
			// off (Framing, noOffset), and the view comes back by itself when the conversation ends. SwitchPOV is not used.
			g_previousPov = g_gameplayPov;
			const bool fp = s.enabled && s.conversationFirstPerson;
			// "Move the conversation camera" in third person: the conversation is held in the player's own third-person view.
			// The game's conversation camera is first person (POV 0) and draws the player's body in its first-person form;
			// moved away from the head, that form showed cut apart up close (the owner, 2026-09-30: "the third person
			// conversation camera while standing closely to the NPC still destroys the player body"). ForceAndLockPOV
			// with the view the player came in with; UnlockAndRestorePOV when the conversation ends.
			const bool own = s.fmGroups[static_cast<std::size_t>(framing::Group::kConversation)].own;
			g_thirdForced = false;
			if (s.enabled && own && !fp && g_gameplayPov != 0) {
				g_thirdForced = ForcePov(a_controller, g_gameplayPov);
				logger::info("conversation: held in third person ({}) - {}", g_gameplayPov == 2 ? "far" : "close",
					g_thirdForced ? "ForceAndLockPOV" : "the controller has no ForceAndLockPOV");
			}
			Status(std::format("in a conversation{}{}", g_speaker ? " with " + g_speakerName : " (no speaker known - the game's aim stands)",
				fp ? "; first person (the game's own conversation view, nothing added)" : g_thirdForced ? "; third person" : ""));
		} else if (g_in && gameplay && !dialogue) {
			g_in = false;
			EndLock();
			if (g_thirdForced) {
				UnlockPov(a_controller);   // the game's own view handling back
				SwitchPov(a_controller, g_previousPov);
				logger::info("conversation: third person released (the view put back: {})", g_previousPov == 2 ? "far" : "close");
				g_thirdForced = false;
			}
			if (g_switched) {
				if (g_forced) {
					UnlockPov(a_controller);   // the game's lock off, the view before it back
				}
				SwitchPov(a_controller, g_previousPov);   // the view the player had before the conversation
				logger::info("conversation: the view put back ({}{})", g_previousPov == 2 ? "far" : "close", g_forced ? ", unlocked" : "");
				g_switched = false;
				g_forced = false;
			}
			Status(g_overridden > 0 ? std::format("the conversation ended (the game's own aim replaced ours {} times)", g_overridden) : "the conversation ended");
			g_speaker = 0;
			g_pawn = {};
			g_mesh = {};
		}

		// the first-person switch, read back: still not first person -> forced and locked for the conversation
		if (g_in && g_switched && g_checkIn > 0 && --g_checkIn == 0) {
			const auto pov = Pov(a_controller);
			if (pov != 0 && !g_forced) {
				g_forced = ForcePov(a_controller, 0);
				logger::info("conversation: the switch to first person did not hold (view {}) - {}", pov,
					g_forced ? "forced and locked with ForceAndLockPOV" : "no ForceAndLockPOV on the controller");
				if (g_forced) g_checkIn = 3;
			} else {
				logger::info("conversation: {} (view {}{})", pov == 0 ? "first person holds" : "first person did NOT hold even forced", pov,
					g_forced ? ", forced" : "");
			}
		}

		// the lock follows the view the player came in with (third person), not the dialogue camera's POV (always 0)
		const bool lock = g_in && s.enabled && s.conversationLockOnSpeaker && !s.conversationFirstPerson && g_speaker && !g_switched && g_gameplayPov != 0;
		if (lock) {
			if (!g_pawnSearched) {
				FindSpeakerPawn(RefOf(g_speaker));
				Status(std::format("in a conversation with {}; the camera holds on them - body {}", g_speakerName, g_pawnMatch));
			}
			Aim(a_controller, a_cameraManager, a_arm);
		} else if (g_aimed) {
			EndLock();   // the lock stopped applying mid-conversation (first person, the switch turned off)
		}
	}

	bool FirstPersonNow() { return g_in && g_switched; }

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "in_conversation", g_in }, { "speaker", g_shownSpeaker }, { "first_person_switched", g_switched },
			{ "speaker_body", g_shownBody }, { "socket", g_shownSocket }, { "arm_held", g_armFrozen },
			{ "aim", g_lastAim }, { "game_replaced_aim", g_overridden } };
	}
}
