#include "Shake.h"

#include "Reflect.h"
#include "Ue.h"

namespace shake
{
	namespace
	{
		using namespace reflect;

		constexpr std::array<const char*, 7> kAmplitudes = {
			"RotOscillation.Pitch.Amplitude", "RotOscillation.Yaw.Amplitude", "RotOscillation.Roll.Amplitude",
			"LocOscillation.X.Amplitude", "LocOscillation.Y.Amplitude", "LocOscillation.Z.Amplitude",
			"FOVOscillation.Amplitude",
		};

		struct Shake
		{
			const wchar_t* path;
			const char*    name;
			UE::UClass*    cls = nullptr;
			bool           recorded = false;
			std::array<float, 7> original{};   // the default object's amplitudes, as the game shipped them
			std::array<bool, 7>  present{};    // the property exists on this class (rule 30: asked, not assumed)
			std::uint32_t  instances = 0;      // live or pooled instances written at the last pass
		};

		std::array<Shake, 2> g_shakes{ {
			{ L"/Game/Dev/CameraShake/BP_SprintCameraShake.BP_SprintCameraShake_C", "third person" },
			{ L"/Game/Dev/CameraShake/BP_SprintCameraShake_FP.BP_SprintCameraShake_FP_C", "first person" },
		} };

		std::optional<bool> g_applied;   // the state last written (nullopt: nothing written yet)
		ULONGLONG           g_nextFind = 0;

		std::mutex  g_lock;
		std::string g_status = "the sprint shakes are not loaded yet";

		void Write(UE::UObject* a_obj, const Shake& a_s, bool a_steady)
		{
			for (std::size_t i = 0; i < kAmplitudes.size(); ++i) {
				if (!a_s.present[i]) continue;
				SetFloat(a_obj, Find(a_obj, kAmplitudes[i]), a_steady ? 0.0f : a_s.original[i]);
			}
		}

		// the default object and every instance of the loaded shake classes
		void Apply(bool a_steady)
		{
			for (auto& s : g_shakes) {
				s.instances = 0;
				if (!s.cls || !s.recorded) continue;
				if (auto* cdo = s.cls->GetDefaultObject(false)) Write(cdo, s, a_steady);
			}
			auto* arr = UE::FUObjectArray::GetSingleton();
			if (!arr) return;
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n; ++i) {
				auto* item = arr->IndexToObject(i);
				auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
				auto* cls = o ? o->GetClass() : nullptr;
				if (!cls) continue;
				for (auto& s : g_shakes) {
					if (s.recorded && cls == s.cls && o != cls->GetDefaultObject(false)) {
						Write(o, s, a_steady);
						++s.instances;
					}
				}
			}
			arr->UnlockInternalArray();
		}

		// the classes load with the first sprint: looked for every 2 s until both are found (rule 17)
		bool FindNew()
		{
			bool found = false;
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextFind) return false;
			g_nextFind = now + 2000;
			for (auto& s : g_shakes) {
				if (s.cls) continue;
				auto* cls = UE::StaticFindObject<UE::UClass>(nullptr, nullptr, s.path);
				auto* cdo = cls ? cls->GetDefaultObject(false) : nullptr;
				if (!cdo) continue;
				s.cls = cls;
				std::string values;
				for (std::size_t i = 0; i < kAmplitudes.size(); ++i) {
					const auto& p = Find(cdo, kAmplitudes[i]);
					s.present[i] = p.Ok();
					s.original[i] = p.Ok() ? GetFloat(cdo, p) : 0.0f;
					values += std::format("{}{}={}", values.empty() ? "" : ", ", kAmplitudes[i], p.Ok() ? std::format("{:.3f}", s.original[i]) : std::string("missing"));
				}
				s.recorded = true;
				found = true;
				logger::info("sprint shake ({}) found - the game's amplitudes: {}", s.name, values);
			}
			return found;
		}
	}

	void Update(bool a_steady)
	{
		const bool found = FindNew();
		if (!found && g_applied == a_steady) return;
		const bool anyLoaded = std::ranges::any_of(g_shakes, [](const Shake& s) { return s.recorded; });
		if (!anyLoaded) {
			// nothing to write yet; a steady request is honoured the moment a class loads (found == true then)
			return;
		}
		Apply(a_steady);
		if (g_applied != a_steady) {
			logger::info("screen shake while sprinting: {}", a_steady ? "off - the sprint shakes' amplitudes are 0" : "on - the game's amplitudes are back");
		}
		g_applied = a_steady;
		std::scoped_lock l(g_lock);
		g_status = std::format("{}; third person {} ({} instances), first person {} ({} instances)",
			a_steady ? "steady" : "the game's shake",
			g_shakes[0].recorded ? "loaded" : "not loaded", g_shakes[0].instances,
			g_shakes[1].recorded ? "loaded" : "not loaded", g_shakes[1].instances);
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status } };
	}
}
