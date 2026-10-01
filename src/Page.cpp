#include "Page.h"

#include <imgui.h>

#include "AMF.h"
#include "PreciseSlider.h"
#include "LockOn.h"
#include "Crosshair.h"
#include "Conversation.h"
#include "Framing.h"
#include "Game.h"
#include "Selection.h"
#include "Settings.h"
#include "Strings.h"

namespace page
{
	namespace
	{
		// An on/off switch (rule 32 - never a checkbox): the framework's own design (ApocryphaMenuFrameworkOR
		// include/utils/ToggleSwitch.h) - a red/green track and a white knob in fixed colours, because AMF's theme leaves
		// Button and FrameBg clear (a theme-coloured track showed only its knob). A normal navigable item for the controller.
		bool Switch(const char* a_label, bool* a_v)
		{
			ImGui::PushID(a_label);
			const float h = ImGui::GetFrameHeight();
			const float w = h * 2.0f;
			const float rr = h * 0.5f;
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton("##switch", ImVec2(w, h));
			if (pressed) *a_v = !*a_v;
			const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			auto* dl = ImGui::GetWindowDrawList();
			const ImU32 track = *a_v ? (hovered ? IM_COL32(92, 191, 96, 255) : IM_COL32(76, 175, 80, 255))
			                         : (hovered ? IM_COL32(207, 84, 84, 255) : IM_COL32(191, 68, 68, 255));
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, rr);
			dl->AddCircleFilled(ImVec2(p.x + rr + (*a_v ? w - h : 0.0f), p.y + rr), rr - 2.0f, IM_COL32(240, 240, 240, 255), 32);
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

		// ---- press-to-bind (1.0.1; the owner's standing rule: a controller button is bound with a listener that takes
		// ANY button, never an INI mask only). AMF's key capture (1.0.2+) arms the next press on one side and keeps it
		// from the menu and the game. A key the framework itself uses, Tab (the game's quick keys), a default of another
		// of our Oblivion Remastered mods (Minimap Menu's K and L, .MD\DEFAULT-KEYS.md) and a key or button already on
		// another CCM action are refused with the reason, and the capture is armed again for the next press.
		struct Capture
		{
			int         row = -1;      // 0 shoulder swap, 1 camera style, 2 CCM on/off, 3 next preset; -1 none
			bool        pad = false;
			int         phase = 0;     // pad: 1 = waiting for every button to be let go (the A that pressed Bind), 2 = armed
			std::string message;
		};
		Capture g_cap;
		constexpr int kBindRows = 5;

		std::int32_t* KeyOf(settings::Values& a_s, int a_row)
		{
			return a_row == 0 ? &a_s.shoulderSwapKey : a_row == 1 ? &a_s.cycleStyleKey : a_row == 2 ? &a_s.toggleKey : a_row == 3 ? &a_s.nextPresetKey : &a_s.lockOnKey;
		}
		std::int32_t* PadOf(settings::Values& a_s, int a_row)
		{
			return a_row == 0 ? &a_s.shoulderSwapButton : a_row == 1 ? &a_s.cycleStyleButton : a_row == 2 ? &a_s.toggleButton : a_row == 3 ? &a_s.nextPresetButton : &a_s.lockOnButton;
		}

		void StartCapture(int a_row, bool a_pad)
		{
			AMF::CancelKeyCapture();
			g_cap.row = a_row;
			g_cap.pad = a_pad;
			if (a_pad) {
				g_cap.phase = 1;
				g_cap.message = TR("BindLetGo", "Let go of every button, then press the new one.");
			} else {
				g_cap.phase = 2;
				AMF::BeginKeyCapture(false, 8000);
				g_cap.message = TR("BindPressKey", "Press a key (Escape cancels).");
			}
			logger::info("keys: capturing the next {} for row {}", a_pad ? "controller button" : "key", a_row);
		}

		void EndCapture(std::string a_message)
		{
			g_cap.row = -1;
			g_cap.phase = 0;
			g_cap.message = std::move(a_message);
		}

		// the reason a_code cannot go on row a_row (empty = it can)
		std::string Refusal(settings::Values& a_s, int a_row, bool a_pad, std::int32_t a_code)
		{
			for (int r = 0; r < kBindRows; ++r) {
				if (r != a_row && (a_pad ? *PadOf(a_s, r) : *KeyOf(a_s, r)) == a_code) {
					return TR("BindRefuseOther", "another CCM action already uses it");
				}
			}
			if (a_pad) return {};
			std::int32_t reserved[32]{};
			const auto   n = AMF::ReservedKeys(reserved, 32);
			for (std::uint32_t i = 0; i < n && i < 32; ++i) {
				if (reserved[i] == a_code) return TR("BindRefuseFramework", "the menu framework uses it");
			}
			if (a_code == 15) return TR("BindRefuseTab", "Tab opens the game's quick keys");
			if (a_code == 37 || a_code == 38) return TR("BindRefuseMinimap", "Minimap Menu uses it by default");
			return {};
		}

		void PollCapture(settings::Values& a_s)
		{
			if (g_cap.row < 0) return;
			if (g_cap.pad && g_cap.phase == 1) {
				bool       ok = false;
				const WORD held = game::PadButtons(&ok);
				if (!ok || held == 0) {   // everything let go (or no pad to read): the next press is the new button
					AMF::BeginKeyCapture(true, 8000);
					g_cap.phase = 2;
					g_cap.message = TR("BindPressButton", "Press a controller button.");
				}
				return;
			}
			std::int32_t kind = 0, code = 0;
			switch (AMF::PollKeyCapture(&kind, &code)) {
			case AMF::CaptureState::kCaptured: {
				const bool wanted = g_cap.pad ? kind == 2 && code > 0 && code <= 0xFFFF : kind == 0 && code > 0 && code <= 255;
				if (!wanted) {
					g_cap.message = g_cap.pad ? TR("BindNotButton", "A stick, a trigger or a key cannot be bound here - press a controller button.")
					                          : TR("BindNotKey", "Only a keyboard key can be bound here - press a key.");
					AMF::BeginKeyCapture(g_cap.pad, 8000);
					return;
				}
				const std::string why = Refusal(a_s, g_cap.row, g_cap.pad, code);
				const std::string name = g_cap.pad ? game::PadName(code) : game::KeyName(code);
				if (!why.empty()) {
					g_cap.message = std::format("{}: {} - {}", name, TR("BindRefused", "cannot be bound"), why);
					logger::info("keys: {} refused for row {} - {}", name, g_cap.row, why);
					AMF::BeginKeyCapture(g_cap.pad, 8000);   // armed again for the next press
					return;
				}
				*(g_cap.pad ? PadOf(a_s, g_cap.row) : KeyOf(a_s, g_cap.row)) = code;
				Changed();
				logger::info("keys: row {} {} is now {} ({})", g_cap.row, g_cap.pad ? "button" : "key", name, code);
				EndCapture(std::format("{} {}", name, TR("BindDone", "bound")));
				return;
			}
			case AMF::CaptureState::kCancelled:
			case AMF::CaptureState::kTimedOut:
				EndCapture(TR("BindNothing", "Nothing pressed - nothing changed."));
				return;
			case AMF::CaptureState::kIdle:
				EndCapture({});   // the framework dropped it (its menu closed)
				return;
			default:
				return;
			}
		}

		void DrawBindings(settings::Values& a_s)
		{
			if (!AMF::HasKeyCapture()) {
				Hint(TR("BindOldFramework", "Binding here needs Apocrypha Menu Framework 1.0.2 or newer. The keys and buttons can still be set in CameraConfigurationMenu.ini."));
			}
			const char* actions[kBindRows] = { TR("BindShoulder", "Move the camera to the other shoulder"), TR("BindCycle", "Switch the free camera style"),
				TR("BindToggle", "Turn CCM on or off"), TR("BindNextPreset", "Load the next preset"), TR("BindLockOn", "Lock on to a target, and let go") };
			const bool can = AMF::HasKeyCapture();
			if (ImGui::BeginTable("##ccmbinds", 3, ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthStretch, 1.6f);
				ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn("pad", ImGuiTableColumnFlags_WidthStretch, 1.2f);
				for (int r = 0; r < kBindRows; ++r) {
					ImGui::PushID(r);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(actions[r]);
					for (int side = 0; side < 2; ++side) {
						const bool pad = side == 1;
						ImGui::TableNextColumn();
						ImGui::PushID(side);
						const bool  waiting = g_cap.row == r && g_cap.pad == pad;
						std::string label = waiting ? std::string(TR("BindWaiting", "Waiting...")) :
						                    pad ? std::format("{}: {}", TR("BindController", "Controller"), game::PadName(*PadOf(a_s, r))) :
						                          std::format("{}: {}", TR("BindKey", "Key"), game::KeyName(*KeyOf(a_s, r)));
						ImGui::BeginDisabled(!can || (g_cap.row >= 0 && !waiting));
						if (ImGui::Button(label.c_str()) && !waiting) StartCapture(r, pad);
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(g_cap.row >= 0 || (pad ? *PadOf(a_s, r) : *KeyOf(a_s, r)) == 0);
						if (ImGui::SmallButton(TR("BindClear", "Clear"))) {
							*(pad ? PadOf(a_s, r) : KeyOf(a_s, r)) = 0;
							Changed();
						}
						ImGui::EndDisabled();
						ImGui::PopID();
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if (g_cap.row >= 0 && ImGui::Button(TR("BindCancel", "Cancel binding"))) {
				AMF::CancelKeyCapture();
				EndCapture(TR("BindNothing", "Nothing pressed - nothing changed."));
			}
			Hint(g_cap.message.empty() ? TR("BindHint", "Select a key or controller button, then press the new one. A controller button also keeps what the game does with it.")
			                          : g_cap.message.c_str());
			PollCapture(a_s);
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
			if (precise::SliderFloat(TR("BlockTurn", "After a block (seconds)"), &s.blockTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (precise::SliderFloat(TR("SpellTurn", "After a spell (seconds)"), &s.spellTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (precise::SliderFloat(TR("AttackTurn", "After an attack or bow shot (seconds)"), &s.attackTurnSeconds, 0.0f, 3.0f, "%.2f")) Changed();
			if (Switch(TR("FaceWhileHeld", "Keep facing while block or attack is held"), &s.faceWhileHeld)) Changed();
			ImGui::EndDisabled();
			ImGui::BeginDisabled(s.cameraStyle == 0);
			if (Switch(TR("FaceWhileLockedOn", "Face the camera while Ultimate Combat is locked on"), &s.faceWhileLockedOn)) Changed();
			Hint(TR("FaceWhileLockedOnHint", "No free camera while a target is locked, so dodges go where you expect."));
			ImGui::EndDisabled();
			ImGui::BeginDisabled(false);
			if (Switch(TR("VanityCamera", "The idle camera that circles you"), &s.vanityCamera)) Changed();
			Hint(TR("VanityCameraHint", "Off: the camera never starts circling your character when you stand idle for a while."));
			ImGui::EndDisabled();

			ImGui::SeparatorText(TR("SectionLockOn", "Lock-on"));
			if (lockon::UltimateCombatOwnsIt()) {
				Hint(TR("LockOnUcrOwns", "Ultimate Combat's own lock-on is switched on in its settings, so it locks on instead and CCM's lock-on stands down. Switch Ultimate Combat's off to use this one."));
			}
			if (Switch(TR("LockOnEnabled", "Lock on to a target"), &s.lockOnEnabled)) Changed();
			Hint(TR("LockOnHint", "Press the lock key (below) to lock the camera on to the person or creature nearest where you look - one fighting you first. Press it again to let go."));
			ImGui::BeginDisabled(!s.lockOnEnabled);
			if (precise::SliderFloat(TR("LockOnRange", "Range"), &s.lockOnRange, 500.0f, 5000.0f, "%.0f")) Changed();
			if (precise::SliderFloat(TR("LockOnAngle", "Angle"), &s.lockOnAngle, 5.0f, 90.0f, "%.0f degrees")) Changed();
			if (precise::SliderFloat(TR("LockOnTurnTime", "Turn time (seconds)"), &s.lockOnTurnTime, 0.0f, 1.0f, "%.2f")) Changed();
			if (precise::SliderFloat(TR("LockOnLookDown", "Look down"), &s.lockOnLookDown, 0.0f, 30.0f, "%.0f degrees")) Changed();
			if (Switch(TR("LockOnStickSwitch", "Flick the right stick to switch target"), &s.lockOnStickSwitch)) Changed();
			if (Switch(TR("LockOnMarker", "Show the target's name"), &s.lockOnMarker)) Changed();
			ImGui::EndDisabled();
			Hint(lockon::Status().c_str());

			ImGui::SeparatorText(TR("SectionKeys", "Keys"));
			DrawBindings(s);

			ImGui::Spacing();
			if (ImGui::Button(TR("Unstick", "Put the game's camera back now"))) game::Queue(game::Action::kUnstick);
			SaveIfSettled();
		}

		// a smoothing slider whose left end (-1) means the game's own value
		bool GameSlider(const char* a_label, float* a_v, float a_max, const char* a_fmt)
		{
			return precise::SliderFloat(a_label, a_v, -1.0f, a_max, *a_v < 0.0f ? TR("GameValue", "the game's") : a_fmt);
		}

		// Framing and smoothing (plan 7.2 / 7.3 - the SmoothCam settings surface, SMOOTHCAM-SETTINGS.md)
		void DrawFraming()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			Hint(TR("FramingIntro", "Where the camera sits in each context, added to the game's own position, in centimetres. 0 is the unmodded camera. Pick a context to edit; \"Now\" shows the one you are in."));

			ImGui::SeparatorText(TR("SectionPosition", "Camera position"));
			const char* groups[static_cast<int>(framing::Group::kCount)] = { TR("GroupStanding", "Standing"), TR("GroupMoving", "Walking or running"), TR("GroupSprinting", "Sprinting"), TR("GroupSneaking", "Sneaking"), TR("GroupWeaponDrawn", "Weapon drawn"), TR("GroupBow", "Bow"), TR("GroupSwimming", "Swimming"), TR("GroupHorseback", "On horseback"), TR("GroupConversation", "In a conversation"), TR("GroupBowAiming", "Aiming a bow") };
			const int now = static_cast<int>(framing::Current());
			ImGui::Text(TR("GroupNow", "Now: %s"), groups[std::clamp(now, 0, static_cast<int>(framing::Group::kCount) - 1)]);
			static int s_edit = 0;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			ImGui::Combo(TR("GroupEdit", "Context"), &s_edit, groups, static_cast<int>(framing::Group::kCount));
			s_edit = std::clamp(s_edit, 0, static_cast<int>(framing::Group::kCount) - 1);
			auto& g = s.fmGroups[static_cast<std::size_t>(s_edit)];
			if (s_edit == 0) {
				Hint(TR("GroupStandingHint", "Standing is also used by every context that has no position of its own."));
			} else if (Switch(TR("GroupOwn", "Its own position (otherwise Standing's)"), &g.own)) {
				Changed();
			}
			ImGui::BeginDisabled(s_edit != 0 && !g.own);
			if (precise::SliderFloat(TR("Side", "Over the shoulder (side)"), &g.side, -150.0f, 150.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("Height", "Height"), &g.height, -100.0f, 150.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("Distance", "Distance behind"), &g.distance, -300.0f, 600.0f, "%.0f cm")) Changed();
			if (s_edit == static_cast<int>(framing::Group::kBowAiming)) {
				if (precise::SliderFloat(TR("AimFov", "Aiming zoom (field of view)"), &g.fov, -40.0f, 40.0f, "%+.0f degrees")) Changed();
				Hint(TR("AimFovHint", "The game zooms in when you draw a bow. Below 0 zooms in further, above 0 less; 0 is the game's."));
			} else if (s_edit == static_cast<int>(framing::Group::kBow)) {
				Hint(TR("BowHint", "Holding a bow. While it is drawn, Aiming a bow is used instead."));
			}
			ImGui::EndDisabled();
			if (s_edit != 0 && ImGui::Button(TR("GroupCopy", "Copy Standing's position here"))) {
				g.side = s.fmGroups[0].side, g.height = s.fmGroups[0].height, g.distance = s.fmGroups[0].distance;
				Changed();
			}
			Hint(TR("ShoulderHint", "[ moves the camera to the other shoulder; the side value mirrors with it."));

			// the game's two third-person zooms (the owner, 2026-09-29)
			ImGui::SeparatorText(TR("SectionZoom", "Zoom"));
			if (Switch(TR("StartZoomedOut", "Start zoomed out"), &s.startZoomedOut)) Changed();
			Hint(TR("StartZoomedOutHint", "After loading a game the camera starts at the far zoom instead of the close one. First person is left as it is."));
			if (Switch(TR("OwnZoom", "Set the zoom distances myself"), &s.ownZoomDistances)) Changed();
			Hint(TR("OwnZoomHint", "The two distances below replace the game's own close and far zoom. Not while aiming a bow or in a conversation. Each context's distance is still added."));
			ImGui::BeginDisabled(!s.ownZoomDistances);
			if (precise::SliderFloat(TR("ZoomClose", "Close zoom"), &s.zoomCloseDistance, 50.0f, 800.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("ZoomFar", "Far zoom"), &s.zoomFarDistance, 50.0f, 1200.0f, "%.0f cm")) Changed();
			ImGui::EndDisabled();
			{
				const auto z = framing::State().value("zoom", json::object());
				const auto pov = z.value("pov", std::string("unknown"));
				ImGui::TextDisabled(TR("ZoomNow", "Now: %s, %.0f cm"), pov == "close" ? TR("ZoomNowClose", "close zoom") : pov == "far" ? TR("ZoomNowFar", "far zoom") : pov == "first person" ? TR("ZoomNowFirst", "first person") : TR("ZoomNowUnknown", "not known yet"),
					z.value("base_now", 0.0));
			}

			ImGui::SeparatorText(TR("SectionSlide", "Moving to a new position"));
			if (Switch(TR("EaseOffsets", "Slide instead of cutting"), &s.smEaseOffsets)) Changed();
			ImGui::BeginDisabled(!s.smEaseOffsets);
			const char* curves[framing::kEasingCount] = {
				TR("EaseLinear", "Linear"),
				TR("EaseQuadIn", "Quadratic in"), TR("EaseQuadOut", "Quadratic out"), TR("EaseQuadInOut", "Quadratic in-out"),
				TR("EaseCubicIn", "Cubic in"), TR("EaseCubicOut", "Cubic out"), TR("EaseCubicInOut", "Cubic in-out"),
				TR("EaseQuartIn", "Quartic in"), TR("EaseQuartOut", "Quartic out"), TR("EaseQuartInOut", "Quartic in-out"),
				TR("EaseQuintIn", "Quintic in"), TR("EaseQuintOut", "Quintic out"), TR("EaseQuintInOut", "Quintic in-out"),
				TR("EaseSineIn", "Sine in"), TR("EaseSineOut", "Sine out"), TR("EaseSineInOut", "Sine in-out"),
				TR("EaseCircIn", "Circular in"), TR("EaseCircOut", "Circular out"), TR("EaseCircInOut", "Circular in-out"),
				TR("EaseExpoIn", "Exponential in"), TR("EaseExpoOut", "Exponential out"), TR("EaseExpoInOut", "Exponential in-out"),
			};
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::Combo(TR("OffsetEasing", "Curve"), &s.smOffsetEasing, curves, framing::kEasingCount)) Changed();
			if (precise::SliderFloat(TR("OffsetSeconds", "Slide time (seconds)"), &s.smOffsetSeconds, 0.0f, 5.0f, "%.2f")) Changed();
			ImGui::EndDisabled();

			ImGui::SeparatorText(TR("SectionSmoothing", "Smoothing"));
			Hint(TR("SmoothingHint", "Slide to the far left for the game's own value. Higher follows more tightly; 0 turns that smoothing off."));
			if (GameSlider(TR("FollowSpeed", "Follow movement"), &s.smFollowSpeed, 30.0f, "%.1f")) Changed();
			if (GameSlider(TR("MaxLagDistance", "Furthest it trails behind (0 = no limit)"), &s.smMaxLagDistance, 500.0f, "%.0f cm")) Changed();
			if (GameSlider(TR("RotationPitch", "Follow looking up and down"), &s.smRotationPitch, 30.0f, "%.1f")) Changed();
			if (GameSlider(TR("RotationYaw", "Follow turning left and right"), &s.smRotationYaw, 30.0f, "%.1f")) Changed();
			if (GameSlider(TR("StateBlend", "Blend between camera states (seconds)"), &s.smStateBlendSeconds, 5.0f, "%.2f")) Changed();
			if (Switch(TR("SprintShake", "Screen shake while sprinting"), &s.smSprintShake)) Changed();
			Hint(TR("SprintShakeHint", "Off keeps the camera steady while you sprint."));

			ImGui::Spacing();
			if (ImGui::Button(TR("FramingReset", "Use the game's position and smoothing"))) {
				const auto d = settings::Defaults();
				std::ranges::copy(d.fmGroups, s.fmGroups);
				s.smFollowSpeed = d.smFollowSpeed; s.smMaxLagDistance = d.smMaxLagDistance; s.smRotationPitch = d.smRotationPitch;
				s.smRotationYaw = d.smRotationYaw; s.smStateBlendSeconds = d.smStateBlendSeconds;
				s.ownZoomDistances = d.ownZoomDistances; s.zoomCloseDistance = d.zoomCloseDistance; s.zoomFarDistance = d.zoomFarDistance;
				Changed();
			}
			SaveIfSettled();
		}

		const char* SelectionReason(selection::Reason a_reason)
		{
			using enum selection::Reason;
			switch (a_reason) {
			case kOff: return TR("SelStateOff", "Selection is off.");
			case kMenu: return TR("SelStateMenu", "Paused while a menu is open.");
			case kOffThird: return TR("SelStateOffThird", "Off in third person.");
			case kOffFirst: return TR("SelStateOffFirst", "Off in first person.");
			case kNoCamera: return TR("SelStateNoCamera", "The camera cannot be read right now.");
			case kCalibrating: return TR("SelStateCalibrating", "Getting ready - walk a few steps.");
			default: return TR("SelStateNotReady", "Waiting for the game.");
			}
		}

		// Better Third-Person Selection, inside CCM (plan 13.2)
		// The conversation camera (the owner: "add to CCM a conversation camera tab") - the Conversation context's own
		// position, and whether the free camera stands down while a conversation runs
		// Presets (1.0.1; plan section 7.5): the three built-ins and six user slots (name, Save, Load, Clear). A preset is
		// the camera's look - style, framing, zoom, smoothing - never the keys, the selection or the crosshair.
		std::array<std::array<char, 48>, settings::kPresetSlots> g_presetNames{};
		bool        g_presetNamesRead = false;
		int         g_clearArmed = 0;   // the slot whose Clear was pressed once; a second press clears it
		std::string g_presetMsg;

		void DrawPresets()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			if (!g_presetNamesRead) {
				g_presetNamesRead = true;
				for (int i = 0; i < settings::kPresetSlots; ++i) {
					const std::string n = settings::PresetExists(i + 1) ? settings::PresetName(i + 1) : std::string();
					std::snprintf(g_presetNames[static_cast<std::size_t>(i)].data(), g_presetNames[static_cast<std::size_t>(i)].size(), "%s", n.c_str());
				}
			}
			Hint(TR("PresetsIntro", "A preset holds the camera's look: the style, the framing, the zoom and the smoothing. Loading one never changes your keys, selection or crosshair settings."));
			ImGui::Text("%s: %s", TR("PresetActive", "Active preset"),
				s.activePreset > 0 && settings::PresetExists(s.activePreset) ? settings::PresetName(s.activePreset).c_str() : TR("PresetNone", "none"));

			ImGui::SeparatorText(TR("PresetsBuiltIn", "Built-in"));
			const char* builtins[3] = { TR("PresetVanilla", "Vanilla"), TR("PresetPlayerCamera", "Player Camera"), TR("PresetPlayerCameraAlt", "Player Camera Alt") };
			for (int b = 0; b < 3; ++b) {
				if (b > 0) ImGui::SameLine();
				if (ImGui::Button(builtins[b])) {
					settings::LoadBuiltin(b);
					g_presetMsg = std::format("{}: {}", builtins[b], TR("PresetLoaded", "loaded"));
				}
			}
			Hint(TR("PresetsBuiltInHint", "Vanilla: the game's own camera. Player Camera: the free camera. Player Camera Alt: the free camera only with the weapon sheathed. All three with no offsets and the game's own smoothing."));

			ImGui::SeparatorText(TR("PresetsSlots", "Your presets"));
			if (ImGui::BeginTable("##ccmpresets", 4, ImGuiTableFlags_SizingStretchProp)) {
				ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
				for (int i = 0; i < settings::kPresetSlots; ++i) {
					ImGui::PushID(i);
					auto&      buf = g_presetNames[static_cast<std::size_t>(i)];
					const int  slot = i + 1;
					const bool has = settings::PresetExists(slot);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-FLT_MIN);
					const std::string hint = std::format("{} {}{}", TR("PresetSlot", "Slot"), slot, has ? "" : std::format(" ({})", TR("PresetEmpty", "empty")));
					ImGui::InputTextWithHint("##name", hint.c_str(), buf.data(), buf.size());
					ImGui::TableNextColumn();
					if (ImGui::Button(TR("PresetSave", "Save"))) {
						const std::string name = buf[0] ? std::string(buf.data()) : std::format("{} {}", TR("PresetSlot", "Slot"), slot);
						g_presetMsg = settings::SavePreset(slot, name) ? std::format("{}: {}", name, TR("PresetSaved", "saved")) : TR("PresetSaveFailed", "The preset could not be written.");
						std::snprintf(buf.data(), buf.size(), "%s", name.c_str());
						g_clearArmed = 0;
					}
					ImGui::TableNextColumn();
					ImGui::BeginDisabled(!has);
					if (ImGui::Button(TR("PresetLoad", "Load"))) {
						g_presetMsg = settings::LoadPreset(slot) ? std::format("{}: {}", settings::PresetName(slot), TR("PresetLoaded", "loaded")) : TR("PresetEmptyMsg", "That slot is empty.");
						g_clearArmed = 0;
					}
					ImGui::EndDisabled();
					ImGui::TableNextColumn();
					ImGui::BeginDisabled(!has);
					const bool armed = g_clearArmed == slot;
					if (ImGui::Button(armed ? TR("PresetClearSure", "Press again to clear") : TR("PresetClear", "Clear"))) {
						if (armed) {
							settings::ClearPreset(slot);
							buf[0] = '\0';
							g_clearArmed = 0;
							g_presetMsg = std::format("{} {}: {}", TR("PresetSlot", "Slot"), slot, TR("PresetCleared", "cleared"));
						} else {
							g_clearArmed = slot;
						}
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			Hint(TR("PresetsNextHint", "\"Load the next preset\" on the Camera tab's keys loads your saved presets in turn."));
			if (!g_presetMsg.empty()) Hint(g_presetMsg.c_str());
		}

		void DrawConversation()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			Hint(TR("ConvIntro", "The camera while you talk to someone. The game's conversation camera is the base; these values are added to it."));
			const bool now = framing::Current() == framing::Group::kConversation;
			ImGui::Text("%s %s", TR("ConvNow", "In a conversation now:"), now ? TR("Yes", "yes") : TR("No", "no"));
			ImGui::TextDisabled("%s", conversation::State().value("status", std::string{}).c_str());
			ImGui::Spacing();
			auto& g = s.fmGroups[static_cast<std::size_t>(framing::Group::kConversation)];
			if (Switch(TR("ConvOwn", "Move the conversation camera (otherwise the game's own)"), &g.own)) Changed();
			Hint(TR("ConvOwnHint", "The game's conversation camera is a first-person view of the person you talk to; these values move it (Side and Height), and Distance pulls it back."));
			ImGui::BeginDisabled(!g.own);
			if (precise::SliderFloat(TR("ConvSide", "Over the shoulder (side)"), &g.side, -150.0f, 150.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("ConvHeight", "Height"), &g.height, -100.0f, 150.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("ConvDistance", "Distance"), &g.distance, -300.0f, 600.0f, "%.0f cm")) Changed();
			if (precise::SliderFloat(TR("ConvFov", "Zoom (field of view)"), &g.fov, -40.0f, 40.0f, "%+.0f degrees")) Changed();
			ImGui::EndDisabled();
			ImGui::SeparatorText(TR("SectionConvView", "View"));
			if (Switch(TR("ConvFirstPerson", "First person in conversations"), &s.conversationFirstPerson)) Changed();
			Hint(TR("ConvFirstPersonHint", "The game's conversation view is first person. On, nothing is added to it; off, the position above moves it (for example over the shoulder)."));
			ImGui::BeginDisabled(!s.conversationFirstPerson);
			if (Switch(TR("ConvFirstPersonNoOffset", "No offset in first person"), &s.conversationFirstPersonNoOffset)) Changed();
			Hint(TR("ConvFirstPersonNoOffsetHint", "The camera position above is not added while you are in first person, so you look straight at the person you talk to."));
			ImGui::EndDisabled();
			ImGui::BeginDisabled(s.conversationFirstPerson);
			if (Switch(TR("ConvLock", "Keep the camera on the person I talk to"), &s.conversationLockOnSpeaker)) Changed();
			Hint(TR("ConvLockHint", "However far the camera is moved over the shoulder, it stays aimed at the person you are talking to (the one you activated)."));
			ImGui::EndDisabled();
			ImGui::SeparatorText(TR("SectionConvCamera", "Free camera"));
			if (Switch(TR("ConvStandDown", "The game's own camera behaviour in conversations"), &s.standDownInDialogue)) Changed();
			Hint(TR("ConvStandDownHint", "On: the free camera stands down while you talk, as the game does it. Off: the free camera stays on."));
			SaveIfSettled();
		}

		void DrawSelection()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			Hint(TR("SelIntro", "Use what you are roughly looking at: anything within reach and within the angle below counts, the closest to where you look first. What the crosshair itself points at always wins."));
			ImGui::Spacing();
			if (Switch(TR("SelEnabled", "Wider selection"), &s.selEnabled)) Changed();
			if (Switch(TR("SelThirdPerson", "In third person"), &s.selThirdPerson)) Changed();
			if (Switch(TR("SelFirstPerson", "In first person"), &s.selFirstPerson)) Changed();
			if (Switch(TR("SelMarker", "Show what will be used, where it is"), &s.selShowMarker)) Changed();
			ImGui::SeparatorText(TR("SelArea", "Area"));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (precise::SliderFloat(TR("SelRange", "Reach"), &s.selRange, 50.0f, 400.0f, "%.0f")) Changed();
			Hint(TR("SelRangeHint", "How far from your character, in game units (about 70 units to a metre)."));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (precise::SliderFloat(TR("SelAngle", "Angle"), &s.selMaxAngle, 5.0f, 75.0f, "%.0f")) Changed();
			Hint(TR("SelAngleHint", "How far to either side of where the camera looks, in degrees."));
			ImGui::Spacing();
			ImGui::Separator();
			const auto st = selection::GetStatus();
			if (!st.active) {
				Hint(SelectionReason(st.reason));
			} else if (!st.choice.empty()) {
				ImGui::TextDisabled(TR("SelSelected", "Selected: %s"), st.choice.c_str());
			} else {
				Hint(TR("SelNothing", "Nothing within reach."));
			}
			SaveIfSettled();
		}

		// the contextual crosshair (plan 13.3)
		void DrawCrosshair()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			const char* modes[3] = { TR("XhModeGame", "The game's crosshair"), TR("XhModeContextual", "Contextual"), TR("XhModeHidden", "Always hidden") };
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::Combo(TR("XhMode", "Crosshair"), &s.xhMode, modes, 3)) Changed();
			switch (s.xhMode) {
			case 0: Hint(TR("XhModeGameHint", "The crosshair as the game shows it.")); break;
			case 1: Hint(TR("XhModeContextualHint", "Hidden until it means something - shown in the moments switched on below.")); break;
			default: Hint(TR("XhModeHiddenHint", "Never shown while you play.")); break;
			}
			ImGui::SeparatorText(TR("XhWhen", "Contextual: shown"));
			ImGui::BeginDisabled(s.xhMode != 1);
			if (Switch(TR("XhWhenAiming", "While aiming a bow or casting a spell"), &s.xhWhenAiming)) Changed();
			if (Switch(TR("XhWhenTarget", "When there is something to use"), &s.xhWhenTarget)) Changed();
			if (Switch(TR("XhWhenWeapon", "While a weapon is drawn"), &s.xhWhenWeaponDrawn)) Changed();
			if (Switch(TR("XhFirstPerson", "Always in first person"), &s.xhInFirstPerson)) Changed();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (precise::SliderFloat(TR("XhFade", "Fade (seconds)"), &s.xhFadeSeconds, 0.0f, 2.0f, "%.2f")) Changed();
			ImGui::EndDisabled();
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
		const bool f = AMF::RegisterPage(kModName, "Framing", &DrawFraming);
		const bool p = AMF::RegisterPage(kModName, "Presets", &DrawPresets);
		const bool v = AMF::RegisterPage(kModName, "Conversation", &DrawConversation);
		const bool c = AMF::RegisterPage(kModName, "Selection", &DrawSelection);
		const bool d = AMF::RegisterPage(kModName, "Crosshair", &DrawCrosshair);
		const bool b = AMF::RegisterPage(kModName, "Status", &DrawStatus);
		logger::info("AMF {} (API {}): pages Camera={}, Framing={}, Presets={}, Conversation={}, Selection={}, Crosshair={}, Status={}", AMF::Version(), AMF::APIVersion(), a, f, p, v, c, d, b);
	}
}
