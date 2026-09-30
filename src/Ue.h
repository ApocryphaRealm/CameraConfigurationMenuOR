#pragma once

// ============================================================================================================
// Just enough Unreal to read the camera (game thread only). CommonLibOB64 has no FProperty, so a property is found
// by NAME by walking a struct's FField chain (next +0x18, name +0x20) and reading FProperty::Offset_Internal at +0x44
// (UE5's layout) - SelfCheck() proves that offset on a property whose place is already known before anything else
// trusts it. Taken from Tween Menu for Oblivion's Reflect/Ue helpers (logic library 7701, 7720), trimmed to what this
// mod reads. Every look-up here is cached by the caller: FName::ToString allocates.
// ============================================================================================================

namespace ue
{
	std::string Utf8(const UE::FString& a_s);
	std::string NameOf(UE::UObject* a_o);

	// -1 when the struct (or its supers) has no property of that name
	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name);

	bool SelfCheck();   // true once the Offset_Internal layout is proven

	// For an object read THIS frame from a live owner: reads a_o's own index, so never for a pointer kept from an
	// earlier frame (a freed object's index is garbage - Improved Wheel Menu crashed in exactly that read, 09:15).
	bool IsLive(UE::UObject* a_o);

	// A pointer kept across frames with the object-array slot it was found in. Get() asks the SLOT whether it still
	// holds that object and never reads the object itself.
	struct Handle
	{
		UE::UObject* ptr = nullptr;
		std::int32_t index = -1;
		void         Set(UE::UObject* a_live);   // a_live must be live now (just found)
		UE::UObject* Get() const;                // nullptr once the slot holds anything else
	};

	// the first live object whose class is a_base or derives from it (not a class default object) - scans the whole
	// object array, so the caller caches the answer
	UE::UObject* FirstOf(UE::UClass* a_base);
	// every live object of a_base or a class derived from it (never a class default object) - a whole scan, so rare
	std::vector<UE::UObject*> AllOf(UE::UClass* a_base);

	inline UE::UClass* Class(const wchar_t* a_path)
	{
		return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path);
	}

	template <class T>
	T* At(void* a_base, std::int32_t a_offset)
	{
		return a_base && a_offset >= 0 ? reinterpret_cast<T*>(static_cast<std::uint8_t*>(a_base) + a_offset) : nullptr;
	}

	// ProcessEvent under a structured-exception guard: false when the engine faulted inside the call - a world-context
	// call whose world a quit or a load tore down (Minimap Menu's crash on quitting, 2026-09-30 01:49:42; gate rule
	// or-world-context-calls-are-guarded)
	bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params);

	// A reflected call: parameters by name, laid out from the UFunction's own properties (Tween Menu's ue::Call).
	class Call
	{
	public:
		Call(UE::UObject* a_obj, const wchar_t* a_fn) :
			m_obj(a_obj),
			m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
		{
			if (m_fn) {
				m_params.assign(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize) + 16, 0);
			}
		}
		explicit operator bool() const { return m_fn != nullptr; }
		void* At(std::string_view a_name)
		{
			if (!m_fn) {
				return nullptr;
			}
			const auto off = Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
			return off >= 0 ? m_params.data() + off : nullptr;
		}
		template <class T>
		bool Set(std::string_view a_name, const T& a_value)
		{
			if (void* p = At(a_name)) {
				std::memcpy(p, &a_value, sizeof(T));
				return true;
			}
			return false;
		}
		template <class T>
		T Get(std::string_view a_name)
		{
			T v{};
			if (void* p = At(a_name)) {
				std::memcpy(&v, p, sizeof(T));
			}
			return v;
		}
		bool Run()
		{
			if (!m_fn || !m_obj) {
				return false;
			}
			m_obj->ProcessEvent(m_fn, m_params.data());
			return true;
		}

		// for a call that takes a world-context object: fault-guarded
		bool RunGuarded() { return m_fn && m_obj && GuardedProcessEvent(m_obj, m_fn, m_params.data()); }

	private:
		UE::UObject*              m_obj;
		UE::UFunction*            m_fn;
		std::vector<std::uint8_t> m_params;
	};

	// A reflected call with no parameters in and one ReturnValue out, laid out from the UFunction's own properties.
	// The UFunction and its ReturnValue offset are looked up once per (class, name) by the caller's static.
	class Getter
	{
	public:
		Getter(const wchar_t* a_function) :
			m_name(a_function)
		{}

		// false when the object has no such function; a_out gets sizeof(T) bytes from ReturnValue
		template <class T>
		bool Get(UE::UObject* a_obj, T& a_out)
		{
			if (!a_obj || !Resolve(a_obj)) {
				return false;
			}
			m_params.assign(m_params.size(), 0);
			a_obj->ProcessEvent(m_fn, m_params.data());
			std::memcpy(&a_out, m_params.data() + m_ret, sizeof(T));
			return true;
		}

	private:
		bool Resolve(UE::UObject* a_obj);

		const wchar_t*            m_name;
		UE::UClass*               m_class = nullptr;
		UE::UFunction*            m_fn = nullptr;
		std::int32_t              m_ret = -1;
		std::vector<std::uint8_t> m_params;
	};
}
