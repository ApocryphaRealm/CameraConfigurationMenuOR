#include "CameraRows.h"

#include "Framing.h"
#include "Settings.h"
#include "Ue.h"

namespace camrows
{
	namespace
	{
		constexpr const wchar_t* kTable = L"/Game/Dev/Data/DT_CameraSettings.DT_CameraSettings";
		using RowMap = UE::TMap<UE::FName, std::uint8_t*>;

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
			std::int32_t tags = -1, closeData = -1, farData = -1;           // in the row
			std::int32_t tagArray = -1, tagSize = 0;                // in FGameplayTagContainer / FGameplayTag
			std::int32_t arm = -1, socket = -1, fov = -1;           // in FVCameraSettingData
		} g_l;

		UE::UObject*                              g_table = nullptr;   // an asset: loaded for the session
		std::vector<std::uint8_t*>                g_blocks;            // the aiming rows' Close / Far data
		std::vector<std::string>                  g_rowNames;
		std::unordered_map<std::uint8_t*, Orig>   g_orig;
		std::array<double, 5>                     g_applied{};         // side, height, distance, fov, mirror
		bool                                      g_on = false;
		ULONGLONG                                 g_nextLook = 0;
		std::string                               g_status = "not looked at yet";
		bool                                      g_scanOk = false;
		ULONGLONG                                 g_nextFind = 0, g_nextRescan = 0;

		void Say(const std::string& a_status, bool a_warn = false)
		{
			if (a_status == g_status) return;
			g_status = a_status;
			if (a_warn) logger::warn("camera table: {}", a_status);
			else logger::info("camera table: {}", a_status);
		}

		// The table: by its path, else among the loaded DataTables by name (a whole-array scan, at most every 5 s until found).
		// Round 9 looked by the path only and said nothing when that missed - no "camera table:" line in the owner's first
		// test (2026-09-30 01:19-01:40), so the aiming rows were never edited.
		UE::UObject* FindTable()
		{
			if (auto* t = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, kTable)) return t;
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextFind) return nullptr;
			g_nextFind = now + 5000;
			static auto* dtClass = ue::Class(L"/Script/Engine.DataTable");
			if (!dtClass) return nullptr;
			for (auto* o : ue::AllOf(dtClass)) {
				if (ue::NameOf(o) == "DT_CameraSettings") return o;
			}
			return nullptr;
		}
		int                                       g_rowsTotal = 0;

		std::string Str(const UE::FName& a_n) { return ue::Utf8(a_n.ToString()); }

		bool FindLayout(UE::UStruct* a_rowStruct)
		{
			auto* data = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/Altar.VCameraSettingData");
			auto* container = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/GameplayTags.GameplayTagContainer");
			auto* tag = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/GameplayTags.GameplayTag");
			if (!a_rowStruct || !data || !container || !tag) return false;
			g_l.tags = ue::Offset(a_rowStruct, "CameraTagsKey");
			g_l.closeData = ue::Offset(a_rowStruct, "ThirdPersonCameraSettingDataClose");
			g_l.farData = ue::Offset(a_rowStruct, "ThirdPersonCameraSettingDataFar");
			g_l.tagArray = ue::Offset(container, "GameplayTags");
			g_l.tagSize = tag->propertiesSize;
			g_l.arm = ue::Offset(data, "DesiredArmLength");
			g_l.socket = ue::Offset(data, "DesiredSocketOffset");
			g_l.fov = ue::Offset(data, "DesiredOverrideFieldOfView");
			g_l.ok = g_l.tags >= 0 && g_l.closeData >= 0 && g_l.farData >= 0 && g_l.tagArray >= 0 && g_l.tagSize >= 8 && g_l.arm >= 0 && g_l.socket >= 0 &&
			         g_l.fov >= 0;
			return g_l.ok;
		}

		// the number of rows the engine itself names (DataTableFunctionLibrary::GetDataTableRowNames); -1 when it cannot say
		int ReflectedRowCount(UE::UObject* a_table)
		{
			auto* lib = ue::Class(L"/Script/Engine.DataTableFunctionLibrary");
			ue::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"GetDataTableRowNames");
			if (!c || !c.At("Table") || !c.At("OutRowNames")) return -1;
			c.Set("Table", a_table);
			c.Run();
			auto* names = static_cast<RawArray*>(c.At("OutRowNames"));
			const int n = names ? names->num : -1;
			if (names && names->data) UE::FMemory::Free(names->data);   // the engine's array (FNames need no destruction)
			return n;
		}

		// the aiming rows' Close and Far data blocks; false (status says why) when the table cannot be read safely
		bool Scan(UE::UObject* a_table)
		{
			g_blocks.clear();
			g_rowNames.clear();
			g_orig.clear();
			g_rowsTotal = 0;
			auto* cls = a_table->GetClass();
			const auto rsOff = cls ? ue::Offset(cls, "RowStruct") : -1;
			if (rsOff < 0) {
				Say("the table has no RowStruct", true);
				return false;
			}
			auto* base = reinterpret_cast<std::uint8_t*>(a_table);
			auto* rowStruct = *reinterpret_cast<UE::UStruct* const*>(base + rsOff);
			if (!rowStruct || !ue::IsLive(rowStruct) || !FindLayout(rowStruct)) {
				Say("the camera row layout is not as expected (FVCameraSettings / FVCameraSettingData)", true);
				return false;
			}
			// RowMap directly after RowStruct (UE 5.3 DataTable.h; TestBench's ue.datatable read, proven in game)
			const auto* map = reinterpret_cast<const RowMap*>(base + rsOff + sizeof(void*));
			const auto& sparse = map->pairs.elements;
			const std::int32_t slots = sparse.data.Num();
			const std::int32_t freeSlots = sparse.numFreeIndices;
			const std::int32_t numBits = sparse.allocationFlags.numBits;
			const std::uint32_t* bits = sparse.allocationFlags.allocatorInstance.GetAllocation();
			if (slots < 0 || slots > 10000 || freeSlots < 0 || freeSlots > slots) {
				Say(std::format("the table's row map is not plausible ({} slots, {} free)", slots, freeSlots), true);
				return false;
			}
			std::vector<std::pair<UE::FName, std::uint8_t*>> rows;
			for (std::int32_t i = 0; i < slots; ++i) {
				if (freeSlots != 0 && !(bits && i < numBits && ((bits[i >> 5] >> (i & 31)) & 1u))) continue;
				const auto& pair = sparse[i].value;
				rows.emplace_back(pair.template Get<0>(), pair.template Get<1>());
			}
			const int reflected = ReflectedRowCount(a_table);
			if (reflected != static_cast<int>(rows.size())) {
				Say(std::format("the row map read {} rows, the engine names {} - the table is left alone", rows.size(), reflected), true);
				return false;
			}
			g_rowsTotal = static_cast<int>(rows.size());
			for (const auto& [name, row] : rows) {
				if (!row) continue;
				const auto* tags = reinterpret_cast<const RawArray*>(row + g_l.tags + g_l.tagArray);
				bool aiming = false;
				std::string tagText;
				for (std::int32_t t = 0; tags->data && t < tags->num && t < 64; ++t) {
					const auto tn = Str(*reinterpret_cast<const UE::FName*>(tags->data + static_cast<std::size_t>(t) * g_l.tagSize));
					aiming = aiming || tn.find("Aiming") != std::string::npos;
					tagText += (tagText.empty() ? "" : ",") + tn;
				}
				if (!aiming) continue;
				g_rowNames.push_back(Str(name) + " [" + tagText + "]");
				for (const auto off : { g_l.closeData, g_l.farData }) {
					std::uint8_t* d = row + off;
					Orig o;
					o.arm = *reinterpret_cast<const float*>(d + g_l.arm);
					const auto* sock = reinterpret_cast<const double*>(d + g_l.socket);
					o.sy = sock[1];
					o.sz = sock[2];
					o.fov = *reinterpret_cast<const float*>(d + g_l.fov);
					g_orig[d] = o;
					g_blocks.push_back(d);
				}
			}
			g_status = std::format("{} of {} rows are aiming rows", g_rowNames.size(), g_rowsTotal);
			logger::info("camera table: {} ({})", g_status, [] {
				std::string s;
				for (const auto& n : g_rowNames) s += (s.empty() ? "" : "; ") + n;
				return s;
			}());
			return true;
		}

		void Write(const std::array<double, 5>& a_v, bool a_on)
		{
			for (auto* d : g_blocks) {
				const auto it = g_orig.find(d);
				if (it == g_orig.end()) continue;
				const Orig& o = it->second;
				auto* arm = reinterpret_cast<float*>(d + g_l.arm);
				auto* sock = reinterpret_cast<double*>(d + g_l.socket);
				auto* fov = reinterpret_cast<float*>(d + g_l.fov);
				if (!a_on) {
					*arm = o.arm, sock[1] = o.sy, sock[2] = o.sz, *fov = o.fov;
					continue;
				}
				*arm = static_cast<float>(std::max(20.0, o.arm + a_v[2]));
				sock[1] = (o.sy + a_v[0]) * a_v[4];
				sock[2] = o.sz + a_v[1];
				*fov = o.fov > 1.0f ? static_cast<float>(std::clamp(o.fov + a_v[3], 20.0, 150.0)) : o.fov;
			}
		}
	}

	void Tick(bool a_enabled, bool a_shoulderLeft)
	{
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextLook) return;
		g_nextLook = now + 250;
		auto* table = FindTable();
		if (!table) {
			Say("DT_CameraSettings is not loaded (looked for by path and by name)");
			g_on = false;
			return;
		}
		// a new table, or a scan that failed: (re)scan - a failed one again every 5 s
		if (table != g_table || (!g_scanOk && now >= g_nextRescan)) {
			if (table != g_table) logger::info("camera table: found {}", ue::NameOf(table));
			g_table = table;
			g_on = false;
			g_nextRescan = now + 5000;
			g_scanOk = Scan(table);
			if (!g_scanOk) {
				g_blocks.clear();
				return;
			}
		}
		if (!g_scanOk) return;
		const auto& s = settings::Get();
		const auto& g = s.fmGroups[static_cast<std::size_t>(framing::Group::kBowAiming)];
		const bool on = a_enabled && g.own && !g_blocks.empty();
		const std::array<double, 5> want{ g.side, g.height, g.distance, g.fov, a_shoulderLeft ? -1.0 : 1.0 };
		if (on == g_on && (!on || want == g_applied)) return;
		Write(want, on);
		logger::info("camera table: the aiming rows {}", on ? std::format("carry side {:+.0f}, height {:+.0f}, distance {:+.0f}, field of view {:+.0f}{}", want[0],
		                                                        want[1], want[2], want[3], want[4] < 0 ? " (left shoulder)" : "")
		                                                    : "are the game's own again");
		g_on = on;
		g_applied = want;
	}

	bool AimingInTable() { return g_on; }

	json State()
	{
		return { { "status", g_status }, { "aiming_rows", g_rowNames }, { "rows_total", g_rowsTotal }, { "aiming_in_table", g_on },
			{ "applied", { { "side", g_applied[0] }, { "height", g_applied[1] }, { "distance", g_applied[2] }, { "fov", g_applied[3] }, { "mirror", g_applied[4] } } } };
	}
}
