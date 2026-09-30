#include "Conversation.h"

#include "Aim.h"
#include "Settings.h"
#include "Ue.h"

namespace conversation
{
	namespace
	{
		bool           g_in = false;
		bool           g_switched = false;
		std::uint8_t   g_previousPov = 1;          // EVPlayerPOVType: 0 first person, 1 third close, 2 third far
		RE::TESFormID  g_lastTalkable = 0;         // the last person or creature the player could activate, in gameplay
		ULONGLONG      g_lastTalkableAt = 0;
		RE::TESFormID  g_speaker = 0;
		std::string    g_speakerName;
		std::array<double, 3> g_lastAim{};
		bool           g_aimed = false;
		int            g_overridden = 0;           // ticks the game's own aim replaced ours (read back the next tick)

		std::mutex  g_lock;
		std::string g_status = "no conversation yet";

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) logger::info("conversation: {}", a_s);
			g_status = std::move(a_s);
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

		bool SwitchPov(UE::UObject* a_ctrl, std::uint8_t a_pov)
		{
			ue::Call c(a_ctrl, L"SwitchPOV");
			if (!c) return false;
			c.Set("TargetPOV", a_pov);
			c.Set("bSetToNewDefaultState", false);
			return c.Run();
		}

		// aim the controller from the camera's own place at the speaker's head
		void Aim(UE::UObject* a_ctrl, UE::UObject* a_mgr)
		{
			auto* ref = g_speaker ? RE::TESForm::LookupByID<RE::TESObjectREFR>(g_speaker) : nullptr;
			if (!ref || !a_ctrl || !a_mgr) return;
			RE::NiPoint3 head = ref->data.location;
			head.z += 115.0f;   // about eye height on a person (Oblivion units)
			UE::FVector target{};
			static ue::Getter camLocation(L"GetCameraLocation");
			std::array<double, 3> cam{};
			if (!aim::ToUnreal(head, target) || !camLocation.Get(a_mgr, cam)) return;
			// the game replaced last tick's aim? (read back before writing again - a write that never holds is reported)
			static ue::Getter controlRotation(L"GetControlRotation");
			std::array<double, 3> now{};
			if (g_aimed && controlRotation.Get(a_ctrl, now) && (std::abs(now[0] - g_lastAim[0]) > 1.0 || std::abs(now[1] - g_lastAim[1]) > 1.0)) {
				++g_overridden;
			}
			const double dx = target.x - cam[0], dy = target.y - cam[1], dz = target.z - cam[2];
			const double yaw = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
			const double pitch = std::atan2(dz, std::hypot(dx, dy)) * 180.0 / std::numbers::pi;
			const std::array<double, 3> rot{ pitch, yaw, 0.0 };
			ue::Call set(a_ctrl, L"SetControlRotation");
			if (!set) return;
			set.Set("NewRotation", rot);
			set.Run();
			g_lastAim = rot;
			g_aimed = true;
		}
	}

	void Tick(UE::UObject* a_controller, UE::UObject* a_cameraManager, const std::string& a_cameraTag)
	{
		const auto& s = settings::Get();
		auto*       im = RE::InterfaceManager::GetInstance(false, false);
		if (!im || !a_controller) return;
		const bool      gameplay = im->menuMode == 1;
		const ULONGLONG now = GetTickCount64();

		if (gameplay && !g_in) {
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
			g_speaker = now - g_lastTalkableAt < 3000 ? g_lastTalkable : 0;
			auto* ref = g_speaker ? RE::TESForm::LookupByID<RE::TESObjectREFR>(g_speaker) : nullptr;
			const char* n = ref && ref->data.objectReference ? RE::TESFullName::GetFullName(ref->data.objectReference) : nullptr;
			g_speakerName = n && *n ? n : (g_speaker ? "(no name)" : "");
			if (s.enabled && s.conversationFirstPerson) {
				g_previousPov = Pov(a_controller);
				if (g_previousPov != 0 && SwitchPov(a_controller, 0)) {
					g_switched = true;
				}
			}
			Status(std::format("in a conversation{}{}", g_speaker ? " with " + g_speakerName : " (no speaker known - the game's aim stands)",
				g_switched ? "; first person" : ""));
		} else if (g_in && gameplay && !dialogue) {
			g_in = false;
			if (g_switched) {
				SwitchPov(a_controller, g_previousPov);   // the view the player had before the conversation
				g_switched = false;
			}
			Status(g_overridden > 0 ? std::format("the conversation ended (the game's own aim replaced ours {} times)", g_overridden) : "the conversation ended");
			g_speaker = 0;
		}

		if (g_in && s.enabled && s.conversationLockOnSpeaker && g_speaker && !g_switched && Pov(a_controller) != 0) {
			Aim(a_controller, a_cameraManager);
		}
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "in_conversation", g_in }, { "speaker", g_speakerName }, { "first_person_switched", g_switched },
			{ "aim", g_lastAim }, { "game_replaced_aim", g_overridden } };
	}
}
