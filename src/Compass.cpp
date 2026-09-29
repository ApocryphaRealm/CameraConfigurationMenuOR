#include "Compass.h"

#include "Ue.h"

namespace compass
{
	namespace
	{
		constexpr const wchar_t* kGetter = L"/Script/Altar.VHUDMainViewModel:GetCompassDirectionValue";

		UE::FNativeFuncPtr  g_original = nullptr;
		UE::UFunction*      g_fn = nullptr;
		std::atomic<bool>   g_active{ false };
		std::atomic<float>  g_value{ 0.0f };
		std::atomic<std::uint64_t> g_calls{ 0 };

		ue::Handle g_marker;   // the cell's BP_NorthMarker_C (indoors), kept by its object-array slot
		ULONGLONG  g_nextMarkerScan = 0;
		bool       g_wasInterior = false;

		std::mutex  g_lock;
		std::string g_status = "not installed yet";
		float       g_lastYaw = 0.0f, g_lastMarker = 0.0f;

		// the native getter's thunk: the game's own value first, then the camera's heading while the free camera is on
		void Thunk(UE::UObject* a_ctx, UE::FFrame& a_stack, const void* a_result)
		{
			if (g_original) {
				g_original(a_ctx, a_stack, a_result);
			}
			g_calls.fetch_add(1, std::memory_order_relaxed);
			if (a_result && g_active.load(std::memory_order_relaxed)) {
				*static_cast<float*>(const_cast<void*>(a_result)) = g_value.load(std::memory_order_relaxed);
			}
		}

		float Normalize(double a_deg)
		{
			double d = std::fmod(a_deg, 360.0);   // floored, never negative (UCR's Lua % - logic library 7552)
			if (d < 0.0) d += 360.0;
			return static_cast<float>(d);
		}

		// the north marker's yaw indoors (0 when there is none)
		double MarkerYaw()
		{
			auto* marker = g_marker.Get();
			if (!marker) {
				const ULONGLONG now = GetTickCount64();
				if (now < g_nextMarkerScan) return 0.0;
				g_nextMarkerScan = now + 2000;
				auto* arr = UE::FUObjectArray::GetSingleton();
				if (!arr) return 0.0;
				arr->LockInternalArray();
				const std::int32_t n = arr->GetObjectArrayNum();
				for (std::int32_t i = 0; i < n && !marker; ++i) {
					auto* item = arr->IndexToObject(i);
					auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
					auto* cls = o ? o->GetClass() : nullptr;
					if (cls && o != cls->GetDefaultObject(false) && ue::NameOf(cls) == "BP_NorthMarker_C") {
						marker = o;
					}
				}
				arr->UnlockInternalArray();
				g_marker.Set(marker);
				if (marker) logger::debug("compass: north marker {}", ue::NameOf(marker));
			}
			if (!marker) return 0.0;
			static ue::Getter rotation(L"K2_GetActorRotation");
			std::array<double, 3> rot{};   // FRotator: pitch, yaw, roll
			return rotation.Get(marker, rot) ? rot[1] : 0.0;
		}
	}

	void Install()
	{
		if (g_fn) return;
		auto* fn = UE::StaticFindObject<UE::UFunction>(nullptr, nullptr, kGetter);
		if (!fn || !fn->func) return;   // not loaded yet: asked again (rule 17)
		g_original = fn->func;
		fn->func = &Thunk;
		g_fn = fn;
		logger::info("compass: GetCompassDirectionValue's native function swapped (previous {:p})", reinterpret_cast<void*>(g_original));
		std::scoped_lock l(g_lock);
		g_status = "installed";
	}

	void Update(UE::UObject* a_controller, bool a_active)
	{
		if (!g_fn || !a_controller || !a_active) {
			g_active.store(false, std::memory_order_relaxed);
			return;
		}
		static ue::Getter controlRotation(L"GetControlRotation");
		std::array<double, 3> rot{};
		if (!controlRotation.Get(a_controller, rot)) {
			g_active.store(false, std::memory_order_relaxed);
			return;
		}
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const bool interior = player && player->GetInterior();
		if (interior != g_wasInterior) {
			g_wasInterior = interior;
			g_marker = {};   // a new cell: its own north marker
			g_nextMarkerScan = 0;
		}
		const double marker = interior ? MarkerYaw() : 0.0;
		const float  value = Normalize(rot[1] + 90.0 - marker);
		g_value.store(value, std::memory_order_relaxed);
		g_active.store(true, std::memory_order_relaxed);
		std::scoped_lock l(g_lock);
		g_lastYaw = static_cast<float>(rot[1]);
		g_lastMarker = static_cast<float>(marker);
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "active", g_active.load() }, { "value", g_value.load() }, { "control_yaw", g_lastYaw },
			{ "north_marker_yaw", g_lastMarker }, { "getter_calls", g_calls.load() } };
	}
}
