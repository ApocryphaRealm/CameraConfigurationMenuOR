#include "Ue.h"

namespace ue
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertyOffsetInternal = 0x44;

		std::atomic<int> g_state{ 0 };   // 0 unchecked, 1 proven, -1 failed

		std::string Narrow(const wchar_t* a_w)
		{
			std::string out;
			for (; a_w && *a_w; ++a_w) {
				out.push_back(*a_w < 0x80 ? static_cast<char>(*a_w) : '?');
			}
			return out;
		}
	}

	std::string Utf8(const UE::FString& a_s)
	{
		const wchar_t* d = UE::GetData(a_s);
		const int      n = UE::GetNum(a_s);
		if (!d || n <= 0) {
			return {};
		}
		const int len = d[n - 1] == L'\0' ? n - 1 : n;
		const int bytes = WideCharToMultiByte(CP_UTF8, 0, d, len, nullptr, 0, nullptr, nullptr);
		std::string out(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
		if (bytes > 0) {
			WideCharToMultiByte(CP_UTF8, 0, d, len, out.data(), bytes, nullptr, nullptr);
		}
		return out;
	}

	std::string NameOf(UE::UObject* a_o)
	{
		return a_o ? Utf8(a_o->GetFName().ToString()) : std::string("null");
	}

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name)
	{
		for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
			for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
				if (Utf8(reinterpret_cast<const UE::FName*>(f + kFieldName)->ToString()) == a_name) {
					return *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal);
				}
			}
		}
		return -1;
	}

	bool SelfCheck()
	{
		if (g_state.load() != 0) {
			return g_state.load() > 0;
		}
		// the same proof Tween Menu and Improved Wheel Menu use: KeyIndex was read in game at 0xD0 (2026-09-26)
		auto* vm = Class(L"/Script/Altar.VQuickKeysMenuViewModel");
		if (!vm) {
			return false;   // not loaded yet: asked again later (rule 17)
		}
		const auto keyIndex = Offset(vm, "KeyIndex");
		g_state.store(keyIndex == 0xD0 ? 1 : -1);
		if (g_state.load() > 0) {
			logger::info("ue: property offsets proven (VQuickKeysMenuViewModel KeyIndex at 0x{:X})", keyIndex);
		} else {
			logger::error("ue: KeyIndex read at 0x{:X}, expected 0xD0 - the property layout is not UE5's; the camera cannot be read", keyIndex);
		}
		return g_state.load() > 0;
	}

	bool IsLive(UE::UObject* a_o)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_o || !arr) {
			return false;
		}
		const std::int32_t idx = a_o->internalIndex;
		if (idx < 0 || idx >= arr->GetObjectArrayNum()) {
			return false;
		}
		auto* item = arr->IndexToObject(idx);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_o;
	}

	void Handle::Set(UE::UObject* a_live)
	{
		ptr = a_live;
		index = a_live ? a_live->internalIndex : -1;
	}

	UE::UObject* Handle::Get() const
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!ptr || !arr || index < 0 || index >= arr->GetObjectArrayNum()) {
			return nullptr;
		}
		auto* item = arr->IndexToObject(index);
		return item && reinterpret_cast<UE::UObject*>(item->object) == ptr ? ptr : nullptr;
	}

	UE::UObject* FirstOf(UE::UClass* a_base)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_base) {
			return nullptr;
		}
		UE::UObject* found = nullptr;
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n && !found; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			auto* cls = o ? o->GetClass() : nullptr;
			if (cls && cls->IsChildOf(a_base) && o != cls->GetDefaultObject(false)) {
				found = o;
			}
		}
		arr->UnlockInternalArray();
		return found;
	}

	bool Getter::Resolve(UE::UObject* a_obj)
	{
		auto* cls = a_obj ? a_obj->GetClass() : nullptr;
		if (!cls) {
			return false;
		}
		if (cls == m_class) {
			return m_fn != nullptr;
		}
		m_class = cls;
		m_fn = a_obj->FindFunction(UE::FName(m_name, UE::EFindName::Find));
		m_ret = -1;
		if (m_fn) {
			auto* st = reinterpret_cast<UE::UStruct*>(m_fn);
			m_ret = Offset(st, "ReturnValue");
			m_params.assign(static_cast<std::size_t>(std::max(st->propertiesSize, 0)), 0);
			if (m_ret < 0 || m_params.size() < static_cast<std::size_t>(m_ret) + 4) {
				logger::warn("ue: {} on {} has no readable ReturnValue", Narrow(m_name), NameOf(cls));
				m_fn = nullptr;
			} else {
				m_params.resize(std::max<std::size_t>(m_params.size(), static_cast<std::size_t>(m_ret) + 32), 0);
				logger::debug("ue: {} on {} - ReturnValue at 0x{:X}, {} parameter bytes", Narrow(m_name), NameOf(cls), m_ret,
					st->propertiesSize);
			}
		} else {
			logger::warn("ue: {} has no function {}", NameOf(cls), Narrow(m_name));
		}
		return m_fn != nullptr;
	}
}
