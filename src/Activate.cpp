#include "Activate.h"

#include "Ue.h"

namespace activate
{
	namespace
	{
		// The Activate action and the context that maps it, from the game's packaged asset list (2026-09-29):
		// Content/Dev/Input/GamePlay/InputActions/Actions/IA_Game_Actions_Activate and InputMappingContexts/IMC_Game_Actions.
		// The first build looked for IA_Game_Default_Activate in IMC_Game_Default - no such action - and never saw a press.
		// IMC_Game_Default is still searched after IMC_Game_Actions, in case a rebind moves it.
		constexpr const wchar_t* kIMCs[] = { L"/Game/Dev/Input/GamePlay/InputMappingContexts/IMC_Game_Actions.IMC_Game_Actions",
			L"/Game/Dev/Input/GamePlay/InputMappingContexts/IMC_Game_Default.IMC_Game_Default" };
		constexpr const char*    kActionName = "IA_Game_Actions_Activate";

		struct RawArray
		{
			std::uint8_t* data;
			std::int32_t  num;
			std::int32_t  max;
		};

		std::mutex               g_lock;   // the fields below, for State() on TestBench's thread
		std::string              g_actionName;
		std::vector<std::string> g_keyNames;
		std::uint64_t            g_presses = 0;

		std::vector<UE::FName> g_keys;   // game thread only

		// the Activate action's keys in IMC_Game_Default now ("None" left out); re-read every 2 s so a rebind is followed
		void RefreshKeys()
		{
			static ULONGLONG last = 0;
			const ULONGLONG  now = GetTickCount64();
			if (last && now - last < 2000) {
				return;
			}
			last = now;
			static std::array<UE::UObject*, std::size(kIMCs)> imcs{};
			bool                                              any = false;
			for (std::size_t i = 0; i < std::size(kIMCs); ++i) {
				if (!imcs[i]) {
					imcs[i] = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, kIMCs[i]);
				}
				any |= imcs[i] != nullptr;
			}
			if (!any) {
				return;   // not loaded yet (rule 17): asked again in 2 s
			}
			auto* st = UE::StaticFindObject<UE::UStruct>(nullptr, nullptr, L"/Script/EnhancedInput.EnhancedActionKeyMapping");
			const std::int32_t size = st ? st->propertiesSize : 0;
			const std::int32_t offAction = st ? ue::Offset(st, "Action") : -1;
			const std::int32_t offKey = st ? ue::Offset(st, "Key") : -1;
			if (size <= 0 || offAction < 0 || offKey < 0) {
				static bool warned = false;
				if (!warned) {
					warned = true;
					logger::warn("activate: the input mapping layout cannot be read - presses are not watched");
				}
				return;
			}
			static bool listed = false;
			std::vector<UE::FName>   keys;
			std::vector<std::string> names;
			std::string              chosen;
			for (auto* imc : imcs) {
			auto* arr = imc && imc->GetClass() ? ue::At<RawArray>(imc, ue::Offset(imc->GetClass(), "Mappings")) : nullptr;
			for (std::int32_t i = 0; arr && i < arr->num; ++i) {
				std::uint8_t* e = arr->data + static_cast<std::ptrdiff_t>(i) * size;
				auto*         act = *reinterpret_cast<UE::UObject**>(e + offAction);
				if (!act) {
					continue;
				}
				const std::string an = ue::NameOf(act);
				if (!listed && an.find("ctivate") != std::string::npos) {
					logger::info("activate: {} maps {} to {}", ue::NameOf(imc), an, ue::Utf8(reinterpret_cast<const UE::FName*>(e + offKey)->ToString()));
				}
				if (an != kActionName) {
					continue;
				}
				chosen = an;
				const auto& key = *reinterpret_cast<const UE::FName*>(e + offKey);
				const std::string kn = ue::Utf8(key.ToString());
				if (kn != "None" && std::ranges::find(names, kn) == names.end()) {
					keys.push_back(key);
					names.push_back(kn);
				}
			}
			}
			listed = true;
			static std::string lastLogged;
			std::string joined;
			for (const auto& n : names) joined += (joined.empty() ? "" : ", ") + n;
			if (joined != lastLogged) {
				lastLogged = joined;
				if (chosen.empty()) {
					logger::warn("activate: no {} in IMC_Game_Actions or IMC_Game_Default - presses are not watched (the actions with 'Activate' in their name are listed above)",
						kActionName);
				} else {
					logger::info("activate: {} is on {}", chosen, joined.empty() ? "no key" : joined);
				}
			}
			g_keys = std::move(keys);
			std::scoped_lock l(g_lock);
			g_actionName = chosen;
			g_keyNames = std::move(names);
		}

		// the player controller's IsInputKeyDown (safe to ask more than once a frame, unlike WasInputKeyJustPressed)
		bool KeyDown(UE::UObject* a_pc, const UE::FName& a_key)
		{
			static UE::UClass*    cls = nullptr;
			static UE::UFunction* fn = nullptr;
			static std::int32_t   offKey = -1, offRet = -1, size = 0;
			if (!a_pc || !a_pc->GetClass()) {
				return false;
			}
			if (a_pc->GetClass() != cls) {
				cls = a_pc->GetClass();
				fn = a_pc->FindFunction(UE::FName(L"IsInputKeyDown", UE::EFindName::Find));
				auto* st = reinterpret_cast<UE::UStruct*>(fn);
				offKey = fn ? ue::Offset(st, "Key") : -1;
				offRet = fn ? ue::Offset(st, "ReturnValue") : -1;
				size = fn ? st->propertiesSize : 0;
				logger::info("activate: IsInputKeyDown {} (Key 0x{:X}, ReturnValue 0x{:X}, {} bytes)", fn ? "found" : "MISSING", offKey, offRet, size);
			}
			if (!fn || offKey < 0 || offRet < 0 || size <= 0) {
				return false;
			}
			std::vector<std::uint8_t> params(static_cast<std::size_t>(size) + 16, 0);
			auto* key = new (params.data() + offKey) UE::FKey(a_key);
			a_pc->ProcessEvent(fn, params.data());
			const bool down = params[static_cast<std::size_t>(offRet)] != 0;
			key->~FKey();   // the engine may attach its key details: released every call
			return down;
		}
	}

	bool PressedThisTick(UE::UObject* a_pc)
	{
		RefreshKeys();
		if (!a_pc) {
			return false;
		}
		// edges per key (the frame tick can run more than once a frame; "just pressed" would fire twice)
		static std::vector<std::pair<std::uint64_t, bool>> s_down;   // FName as its 64-bit value -> down last tick
		bool pressed = false;
		for (const auto& key : g_keys) {
			std::uint64_t id = 0;
			std::memcpy(&id, &key, std::min(sizeof(id), sizeof(key)));
			const bool down = KeyDown(a_pc, key);
			auto it = std::ranges::find_if(s_down, [&](const auto& e) { return e.first == id; });
			if (it == s_down.end()) {
				s_down.emplace_back(id, down);
				continue;
			}
			if (down && !it->second) {
				pressed = true;
			}
			it->second = down;
		}
		if (pressed) {
			std::scoped_lock l(g_lock);
			++g_presses;
		}
		return pressed;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "action", g_actionName }, { "keys", g_keyNames }, { "presses", g_presses } };
	}
}
