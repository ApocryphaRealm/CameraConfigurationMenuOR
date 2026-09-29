#include "Page.h"

#include <imgui.h>

#include "AMF.h"
#include "Game.h"
#include "Settings.h"
#include "Strings.h"

namespace page
{
	namespace
	{
		// An on/off switch (rule 32 - never a checkbox). AMF.h has no toggle export yet (queued for AMF); this draws the
		// same track-and-knob shape in the theme's own colours, and is a normal navigable item for the controller.
		bool Switch(const char* a_label, bool* a_v)
		{
			ImGui::PushID(a_label);
			const float h = ImGui::GetFrameHeight();
			const float w = h * 1.8f;
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton("##switch", ImVec2(w, h));
			if (pressed) *a_v = !*a_v;
			const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			auto* dl = ImGui::GetWindowDrawList();
			const ImU32 track = ImGui::GetColorU32(*a_v ? (hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button) : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, h * 0.5f);
			const float r = h * 0.5f - 2.0f;
			const float cx = *a_v ? p.x + w - r - 2.0f : p.x + r + 2.0f;
			dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, ImGui::GetColorU32(*a_v ? ImGuiCol_Text : ImGuiCol_TextDisabled));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_label);
			ImGui::PopID();
			return pressed;
		}

		void Hint(const char* a_text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", a_text);
			ImGui::PopStyleColor();
		}

		bool g_dirty = false;
		std::chrono::steady_clock::time_point g_dirtySince{};

		void Changed()
		{
			g_dirty = true;
			g_dirtySince = std::chrono::steady_clock::now();
		}

		// Sliders change every frame while dragged: the INI is written once they have been still for half a second.
		void SaveIfSettled()
		{
			if (g_dirty && std::chrono::steady_clock::now() - g_dirtySince > 500ms && !ImGui::IsAnyItemActive()) {
				g_dirty = false;
				settings::Save();
			}
		}

		bool Begin()
		{
			if (!AMF::UseFrameworkImGui()) return false;
			strings::Refresh();
			game::NotePageDrawn();
			return true;
		}

		void DrawCamera()
		{
			if (!Begin()) return;
			auto& s = settings::Get();

			if (Switch(TR("Enabled", "Camera Configuration Menu on"), &s.enabled)) Changed();
			ImGui::Spacing();

			const char* styles[3] = { TR("StyleVanilla", "The game's camera"), TR("StyleFree", "Free camera"), TR("StyleFreeSheathed", "Free camera, weapon sheathed") };
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::Combo(TR("Style", "Camera style"), &s.cameraStyle, styles, 3)) Changed();
			switch (s.cameraStyle) {
			case 0: Hint(TR("StyleVanillaHint", "The camera turns your character, as in the unmodded game.")); break;
			case 1: Hint(TR("StyleFreeHint", "The camera circles freely and your character turns to where it walks. Blocking, casting and attacking turn it to face where the camera looks.")); break;
			default: Hint(TR("StyleFreeSheathedHint", "The free camera while your weapon is away; with a weapon drawn, your character faces where the camera looks.")); break;
			}

			ImGui::SeparatorText(TR("SectionFacing", "Turning to face the camera"));
			ImGui::BeginDisabled(s.cameraStyle != 1);
			if (ImGui::SliderFloat(TR("BlockTurn", "After a block (seconds)"), &s.blockTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (ImGui::SliderFloat(TR("SpellTurn", "After a spell (seconds)"), &s.spellTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (ImGui::SliderFloat(TR("AttackTurn", "After an attack or bow shot (seconds)"), &s.attackTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (Switch(TR("FaceWhileHeld", "Keep facing while block or attack is held"), &s.faceWhileHeld)) Changed();
			ImGui::EndDisabled();

			ImGui::SeparatorText(TR("SectionKeys", "Keys"));
			Hint(TR("KeysHint", "K moves the camera to the other shoulder. L switches between the two free camera styles."));

			ImGui::Spacing();
			if (ImGui::Button(TR("Unstick", "Put the game's camera back now"))) game::Queue(game::Action::kUnstick);
			SaveIfSettled();
		}

		const char* YesNo(bool a_b) { return a_b ? TR("Yes", "yes") : TR("No", "no"); }

		void Row(const char* a_label, const std::string& a_value)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_label);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(a_value.empty() ? TR("Waiting", "waiting") : a_value.c_str());
		}

		void DrawStatus()
		{
			if (!Begin()) return;
			const auto st = game::Status();
			Hint(TR("StatusHint", "Live values, read from the game as it runs."));
			if (!st.problem.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.35f, 1.0f));
				ImGui::TextWrapped("%s", st.problem.c_str());
				ImGui::PopStyleColor();
			}
			if (ImGui::BeginTable("status", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
				Row(TR("RowMode", "Camera now"), st.mode);
				Row(TR("RowState", "Game camera state"), st.cameraTag);
				Row(TR("RowHook", "Engine hook"), st.hookInstalled ? st.processEvent : std::string{});
				Row(TR("RowTicks", "Updates per second"), std::format("{:.0f}  ({:.1f} us each)", st.ticksPerSecond, st.tickMicros));
				Row(TR("RowPlayer", "Player"), st.pawn);
				Row(TR("RowController", "Controller"), st.controller);
				Row(TR("RowArm", "Camera arm"), st.arm);
				Row(TR("RowManager", "Camera manager"), st.cameraManager);
				Row(TR("RowFirstPerson", "First person"), YesNo(st.firstPerson));
				Row(TR("RowWeapon", "Weapon drawn"), YesNo(st.combatStance));
				Row(TR("RowFacing", "Facing the camera"), st.locked ? std::format("{} ({:.2f} s)", YesNo(true), st.lockRemaining) : YesNo(false));
				Row(TR("RowSwitches", "Switches (read back)"), std::format("{} {} {} {}", st.switches[0] ? 1 : 0, st.switches[1] ? 1 : 0, st.switches[2] ? 1 : 0, st.switches[3] ? 1 : 0));
				Row(TR("RowVanilla", "The game's switches"), st.vanillaRecorded ? std::format("{} {} {} {}", st.vanilla[0] ? 1 : 0, st.vanilla[1] ? 1 : 0, st.vanilla[2] ? 1 : 0, st.vanilla[3] ? 1 : 0) : std::string{});
				Row(TR("RowShoulder", "Shoulder"), st.shoulderLeft ? TR("Left", "left") : TR("Right", "right"));
				Row(TR("RowSocket", "Camera offset (game, now)"), std::format("{:.0f} {:.0f} {:.0f} / {:.0f} {:.0f} {:.0f}", st.socketBase[0], st.socketBase[1], st.socketBase[2], st.socketNow[0], st.socketNow[1], st.socketNow[2]));
				Row(TR("RowDistance", "Camera distance (wanted, now)"), std::format("{:.0f} / {:.0f}", st.desiredArmLength, st.armLengthNow));
				Row(TR("RowFov", "Field of view"), std::format("{:.0f}", st.fov));
				Row(TR("RowEvents", "Block / attack / cast events"), std::format("{} / {} / {}", st.blockEvents, st.attackEvents, st.castEvents));
				std::string tags;
				for (const auto& t : st.tagsSeen) tags += (tags.empty() ? "" : ", ") + t;
				Row(TR("RowTagsSeen", "Camera states seen"), tags);
				ImGui::EndTable();
			}
		}
	}

	void Register()
	{
		if (!AMF::IsInstalled()) {
			logger::info("the Apocrypha Menu Framework is not installed - no menu page; CCM runs from its INI");
			return;
		}
		const bool a = AMF::RegisterPage(kModName, "Camera", &DrawCamera);
		const bool b = AMF::RegisterPage(kModName, "Status", &DrawStatus);
		logger::info("AMF {} (API {}): pages Camera={}, Status={}", AMF::Version(), AMF::APIVersion(), a, b);
	}
}
