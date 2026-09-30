#include "CameraRows.h"

#include "Framing.h"
#include "Settings.h"
#include "Ue.h"

namespace camrows
{
	namespace
	{
		// Each camera state of the camera manager's state machine keeps its camera in an ASP_CameraSettings_C object
		// (property CameraSettings, an FVCameraSettings: CameraTagsKey and the First-person / Third-person Close / Far
		// FVCameraSettingData). Found by the primary session in the running game (2026-09-30 01:5x): 20 live ones,
		// ...PersistentLevel.BP_AltarPlayerCameraManager_C_<n>.State Machine.AST_<State>_C_<n>.ASP_CameraSettings_C_<k>
		// - there is NO camera DataTable at run time (round 9 looked for DT_CameraSettings and found nothing). While a
		// state's tag holds, the game re-applies that object's data every frame: the live arm at 01:00:14 was exactly
		// Standing_Aiming's Far data (240, socket (0,45,20)).
		constexpr const wchar_t* kClass = L"/Game/Dev/StateMachine/Camera/ASP_CameraSettings.ASP_CameraSettings_C";
		constexpr double         kAimingFov = 75.0;   // the view's field of view while aiming, as read in game

		struct RawArray
		{
			std::uint8_t* data;
			std::int32_t  num;
			std::int32_t  max;
		};

		struct Orig
		{
			float  arm = 0.0f;
			double sy = 0.0, sz = 0.0;
			float  fov = 0.0f;
		};

		struct Layout
		{
			bool         ok = false;
			std::int32_t settings = -1;                          // CameraSettings in the object
			std::int32_t tags = -1, closeData = -1, farData = -1;   // in FVCameraSettings
			std::int32_t tagArray = -1, tagSize = 0;                // in FGameplayTagContainer / FGameplayTag
			std::int32_t arm = -1, socket = -1, fov = -1;           // in FVCameraSettingData
		} g_l;

		std::vector<ue::Handle>                 g_objects;   // the aiming states' settings objects, kept by their slots
		std::vector<std::uint8_t*>              g_blocks;    // their Close / Far data (checked through g_objects)
		std::vector<std::string>                g_stateNames;
		std::unordered_map<std::uint8_t*, Orig> g_orig;
		std::array<double, 5>                   g_applied{};   // side, height, distance, fov, mirror
		bool                                    g_on = false;
		ULONGLONG                               g_nextLook = 0, g_nextScan = 0;
		std::string                             g_status = "not looked at yet";
		int                                     g_total = 0;

		void Say(const std::string& a_status, bool a_warn = false)
		{
			if (a_status == g_status) return;
			g_status = a_status;
			if (a_warn) logger::warn("camera states: {}", a_status);
			else logger::info("camera states: {}", a_status);
		}

		std::string Str(const UE::FName& a_n) { return ue::Utf8(a_n.ToString()); }

		bool FindLayout(UE::UClass* a_class)
		{
			auto* settings = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.VCameraSettings");
			auto* data = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.VCameraSettingData");
			auto* container = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/GameplayTags.GameplayTagContainer");
			auto* tag = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/GameplayTags.GameplayTag");
			if (!a_class || !settings || !data || !container || !tag) return false;
			g_l.settings = ue::Offset(a_class, "CameraSettings");
			g_l.tags = ue::Offset(settings, "CameraTagsKey");
			g_l.closeData = ue::Offset(settings, "ThirdPersonCameraSettingDataClose");
			g_l.farData = ue::Offset(settings, "ThirdPersonCameraSettingDataFar");
			g_l.tagArray = ue::Offset(container, "GameplayTags");
			g_l.tagSize = tag->propertiesSize;
			g_l.arm = ue::Offset(data, "DesiredArmLength");
			g_l.socket = ue::Offset(data, "DesiredSocketOffset");
			g_l.fov = ue::Offset(data, "DesiredOverrideFieldOfView");
			g_l.ok = g_l.settings >= 0 && g_l.tags >= 0 && g_l.closeData >= 0 && g_l.farData >= 0 && g_l.tagArray >= 0 && g_l.tagSize >= 8 &&
			         g_l.arm >= 0 && g_l.socket >= 0 && g_l.fov >= 0;
			return g_l.ok;
		}

		// a template (a sub-object of a class default object: ...Default__AST_<State>_C:ASP_CameraSettings_C_<k>) is not a
		// live state - only the camera manager's own instances are edited
		bool IsTemplate(UE::UObject* a_o)
		{
			for (auto* o = a_o ? a_o->GetOuter() : nullptr; o; o = o->GetOuter()) {
				if (ue::NameOf(o).starts_with("Default__")) return true;
			}
			return false;
		}

		void Restore()
		{
			for (auto* d : g_blocks) {
				const auto it = g_orig.find(d);
				if (it == g_orig.end()) continue;
				const Orig& o = it->second;
				*reinterpret_cast<float*>(d + g_l.arm) = o.arm;
				auto* sock = reinterpret_cast<double*>(d + g_l.socket);
				sock[1] = o.sy, sock[2] = o.sz;
				*reinterpret_cast<float*>(d + g_l.fov) = o.fov;
			}
		}

		bool StillLive()
		{
			for (const auto& h : g_objects) {
				if (!h.Get()) return false;
			}
			return !g_objects.empty();
		}

		// the aiming (and bow zoom) states' settings objects; false (said why) when none can be used
		bool Scan()
		{
			g_objects.clear();
			g_blocks.clear();
			g_stateNames.clear();
			g_orig.clear();
			g_total = 0;
			auto* cls = ue::Class(kClass);
			if (!cls) {
				Say("the camera state settings class is not loaded yet");
				return false;
			}
			if (!g_l.ok && !FindLayout(cls)) {
				Say("the camera state settings layout is not as expected (ASP_CameraSettings_C.CameraSettings / VCameraSettings)", true);
				return false;
			}
			for (auto* o : ue::AllOf(cls)) {
				if (IsTemplate(o)) continue;
				++g_total;
				auto* base = reinterpret_cast<std::uint8_t*>(o) + g_l.settings;
				const auto* tags = reinterpret_cast<const RawArray*>(base + g_l.tags + g_l.tagArray);
				bool aiming = false;
				std::string tagText;
				for (std::int32_t t = 0; tags->data && t < tags->num && t < 64; ++t) {
					const auto tn = Str(*reinterpret_cast<const UE::FName*>(tags->data + static_cast<std::size_t>(t) * g_l.tagSize));
					aiming = aiming || tn.find("Aiming") != std::string::npos || tn.find("Zooming") != std::string::npos;
					tagText += (tagText.empty() ? "" : ",") + tn;
				}
				if (!aiming) continue;
				ue::Handle h;
				h.Set(o);
				g_objects.push_back(h);
				g_stateNames.push_back(tagText);
				for (const auto off : { g_l.closeData, g_l.farData }) {
					std::uint8_t* d = base + off;
					Orig og;
					og.arm = *reinterpret_cast<const float*>(d + g_l.arm);
					const auto* sock = reinterpret_cast<const double*>(d + g_l.socket);
					og.sy = sock[1];
					og.sz = sock[2];
					og.fov = *reinterpret_cast<const float*>(d + g_l.fov);
					g_orig[d] = og;
					g_blocks.push_back(d);
				}
			}
			if (g_objects.empty()) {
				Say(std::format("{} camera states, none of them aiming - nothing to edit", g_total), true);
				return false;
			}
			std::string names;
			for (const auto& n : g_stateNames) names += (names.empty() ? "" : "; ") + n;
			Say(std::format("{} of {} camera states are aiming states ({})", g_objects.size(), g_total, names));
			return true;
		}

		void Write(const std::array<double, 5>& a_v)
		{
			for (auto* d : g_blocks) {
				const auto it = g_orig.find(d);
				if (it == g_orig.end()) continue;
				const Orig& o = it->second;
				*reinterpret_cast<float*>(d + g_l.arm) = static_cast<float>(std::max(20.0, o.arm + a_v[2]));
				auto* sock = reinterpret_cast<double*>(d + g_l.socket);
				sock[1] = (o.sy + a_v[0]) * a_v[4];
				sock[2] = o.sz + a_v[1];
				// the aiming states carry no field of view of their own (0); the view while aiming read 75 in game (captures
				// 00:58 / 01:00), so an Aiming zoom is laid on 75 there, and a state with its own value keeps it as the base
				const double fovBase = o.fov > 1.0f ? o.fov : kAimingFov;
				*reinterpret_cast<float*>(d + g_l.fov) = std::abs(a_v[3]) > 0.01 ? static_cast<float>(std::clamp(fovBase + a_v[3], 20.0, 150.0)) : o.fov;
			}
		}
	}

	void Tick(bool a_enabled, bool a_shoulderLeft)
	{
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextLook) return;
		g_nextLook = now + 250;
		// the states are the camera manager's: a new manager (a load, a new game) has new ones - checked through the kept
		// slots every look, and the object array scanned again at most every 5 s while none is usable
		if (!StillLive()) {
			g_on = false;
			g_objects.clear();
			g_blocks.clear();
			if (now < g_nextScan) return;
			g_nextScan = now + 5000;
			if (!Scan()) return;
		}
		const auto& s = settings::Get();
		const auto& g = s.fmGroups[static_cast<std::size_t>(framing::Group::kBowAiming)];
		const bool on = a_enabled && g.own;
		const std::array<double, 5> want{ g.side, g.height, g.distance, g.fov, a_shoulderLeft ? -1.0 : 1.0 };
		if (on == g_on && (!on || want == g_applied)) return;
		if (on) Write(want);
		else Restore();
		logger::info("camera states: the aiming states {}", on ? std::format("carry side {:+.0f}, height {:+.0f}, distance {:+.0f}, field of view {:+.0f}{}", want[0],
		                                                         want[1], want[2], want[3], want[4] < 0 ? " (left shoulder)" : "")
		                                                     : "are the game's own again");
		g_on = on;
		g_applied = want;
	}

	bool AimingInTable() { return g_on; }

	json State()
	{
		return { { "status", g_status }, { "aiming_states", g_stateNames }, { "states_total", g_total }, { "aiming_in_table", g_on },
			{ "applied", { { "side", g_applied[0] }, { "height", g_applied[1] }, { "distance", g_applied[2] }, { "fov", g_applied[3] }, { "mirror", g_applied[4] } } } };
	}
}
