#include "Reflect.h"

namespace reflect
{
	namespace
	{
		template <class T>
		T At(const void* a_base, std::size_t a_off) { return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(a_base) + a_off); }

		std::string FieldClass(const UE::FField* a_f)
		{
			if (!a_f || !a_f->classPrivate) return {};
			return Name(*reinterpret_cast<const UE::FName*>(a_f->classPrivate));   // FFieldClass: its FName comes first
		}

		const UE::FField* Child(const UE::UStruct* a_s, std::string_view a_name)
		{
			for (const UE::UStruct* s = a_s; s; s = s->GetSuperStruct()) {
				for (const UE::FField* f = s->childProperties; f; f = f->next) {
					if (Name(f->namePrivate) == a_name) return f;
				}
			}
			return nullptr;
		}

		std::mutex g_lock;   // the page thread reads CachedCount; everything else is the game thread
		std::unordered_map<std::string, Prop> g_cache;
		const Prop kNone{};
	}

	std::string Utf8(const UE::FString& a_s)
	{
		const wchar_t* d = UE::GetData(a_s);
		const int n = UE::GetNum(a_s);
		if (!d || n <= 0) return {};
		const int len = d[n - 1] == L'\0' ? n - 1 : n;
		const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, d, len, nullptr, 0, nullptr, nullptr);
		std::string out(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
		if (bytes > 0) ::WideCharToMultiByte(CP_UTF8, 0, d, len, out.data(), bytes, nullptr, nullptr);
		return out;
	}

	std::string Name(const UE::FName& a_n) { return Utf8(a_n.ToString()); }

	std::string ClassName(const UE::UObject* a_o)
	{
		return a_o && a_o->GetClass() ? Name(a_o->GetClass()->GetFName()) : std::string{};
	}

	const Prop& Find(const UE::UObject* a_obj, std::string_view a_path)
	{
		if (!a_obj || !a_obj->GetClass()) return kNone;
		const std::string key = std::format("{:p}|{}", static_cast<const void*>(a_obj->GetClass()), a_path);
		std::scoped_lock l(g_lock);
		if (const auto it = g_cache.find(key); it != g_cache.end()) return it->second;

		Prop p;
		const UE::UStruct* owner = a_obj->GetClass();
		std::int32_t base = 0;
		std::string_view rest = a_path;
		std::string why;
		while (!rest.empty()) {
			const auto dot = rest.find('.');
			const std::string_view part = rest.substr(0, dot);
			rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
			const UE::FField* f = Child(owner, part);
			if (!f) { why = std::format("no property '{}' on {}", part, Name(owner->GetFName())); break; }
			const std::int32_t off = At<std::int32_t>(f, Layout::kOffset);
			const std::int32_t size = At<std::int32_t>(f, Layout::kElementSize);
			if (off < 0 || size <= 0 || off + size > owner->propertiesSize) {
				why = std::format("'{}' offset 0x{:X} size {} does not fit {} (0x{:X})", part, off, size, Name(owner->GetFName()), owner->propertiesSize);
				break;
			}
			const std::string cls = FieldClass(f);
			if (!rest.empty()) {
				if (cls != "StructProperty") { why = std::format("'{}' is a {}, not a struct", part, cls); break; }
				const auto* inner = At<const UE::UStruct*>(f, Layout::kSubclass);
				if (!inner || inner->propertiesSize > size + 16) { why = std::format("'{}' struct pointer failed its size check", part); break; }
				owner = inner;
				base += off;
				continue;
			}
			p.offset = base + off;
			p.size = size;
			if (cls == "BoolProperty") {
				p.kind = Kind::kBool;
				p.boolByte = At<std::uint8_t>(f, Layout::kSubclass + 1);
				p.boolMask = At<std::uint8_t>(f, Layout::kSubclass + 3);
			} else if (cls == "FloatProperty" && size == 4) {
				p.kind = Kind::kFloat;
			} else if (cls == "DoubleProperty" && size == 8) {
				p.kind = Kind::kDouble;
			} else if (cls == "IntProperty" && size == 4) {
				p.kind = Kind::kInt;
			} else if (cls == "NameProperty" && size == 8) {
				p.kind = Kind::kName;
			} else if ((cls == "ObjectProperty" || cls == "ClassProperty") && size == 8) {
				p.kind = Kind::kObject;
			} else if (cls == "StructProperty") {
				const auto* inner = At<const UE::UStruct*>(f, Layout::kSubclass);
				const std::string sn = inner ? Name(inner->GetFName()) : "";
				p.kind = sn == "Vector" && size == 24 ? Kind::kVector : sn == "Rotator" && size == 24 ? Kind::kRotator : Kind::kStruct;
			} else {
				p.kind = Kind::kOther;
			}
		}
		if (!p.Ok()) {
			logger::warn("reflect: {} on {} - {}", a_path, Name(a_obj->GetClass()->GetFName()), why);
		} else {
			logger::debug("reflect: {}.{} at 0x{:X} ({} bytes, kind {})", Name(a_obj->GetClass()->GetFName()), a_path, p.offset, p.size, static_cast<int>(p.kind));
		}
		return g_cache.emplace(key, p).first->second;
	}

	bool GetBool(UE::UObject* a_obj, const Prop& a_p)
	{
		if (!a_obj || a_p.kind != Kind::kBool) return false;
		return (At<std::uint8_t>(a_obj, static_cast<std::size_t>(a_p.offset) + a_p.boolByte) & a_p.boolMask) != 0;
	}

	void SetBool(UE::UObject* a_obj, const Prop& a_p, bool a_v)
	{
		if (!a_obj || a_p.kind != Kind::kBool) return;
		auto* b = reinterpret_cast<std::uint8_t*>(a_obj) + a_p.offset + a_p.boolByte;
		*b = a_v ? static_cast<std::uint8_t>(*b | a_p.boolMask) : static_cast<std::uint8_t>(*b & ~a_p.boolMask);
	}

	float GetFloat(UE::UObject* a_obj, const Prop& a_p)
	{
		if (!a_obj) return 0.0f;
		if (a_p.kind == Kind::kFloat) return At<float>(a_obj, static_cast<std::size_t>(a_p.offset));
		if (a_p.kind == Kind::kDouble) return static_cast<float>(At<double>(a_obj, static_cast<std::size_t>(a_p.offset)));
		return 0.0f;
	}

	void SetFloat(UE::UObject* a_obj, const Prop& a_p, float a_v)
	{
		if (!a_obj) return;
		auto* at = reinterpret_cast<std::uint8_t*>(a_obj) + a_p.offset;
		if (a_p.kind == Kind::kFloat) *reinterpret_cast<float*>(at) = a_v;
		else if (a_p.kind == Kind::kDouble) *reinterpret_cast<double*>(at) = a_v;
	}

	UE::UObject* GetObject(UE::UObject* a_obj, const Prop& a_p)
	{
		if (!a_obj || a_p.kind != Kind::kObject) return nullptr;
		auto* o = At<UE::UObject*>(a_obj, static_cast<std::size_t>(a_p.offset));
		return IsLive(o) ? o : nullptr;
	}

	std::string GetName(UE::UObject* a_obj, const Prop& a_p)
	{
		if (!a_obj || a_p.kind != Kind::kName) return {};
		return Name(*reinterpret_cast<const UE::FName*>(reinterpret_cast<const std::uint8_t*>(a_obj) + a_p.offset));
	}

	std::array<double, 3> GetVec(UE::UObject* a_obj, const Prop& a_p)
	{
		if (!a_obj || (a_p.kind != Kind::kVector && a_p.kind != Kind::kRotator)) return {};
		const auto* d = reinterpret_cast<const double*>(reinterpret_cast<const std::uint8_t*>(a_obj) + a_p.offset);
		return { d[0], d[1], d[2] };
	}

	void SetVec(UE::UObject* a_obj, const Prop& a_p, const std::array<double, 3>& a_v)
	{
		if (!a_obj || (a_p.kind != Kind::kVector && a_p.kind != Kind::kRotator)) return;
		auto* d = reinterpret_cast<double*>(reinterpret_cast<std::uint8_t*>(a_obj) + a_p.offset);
		d[0] = a_v[0]; d[1] = a_v[1]; d[2] = a_v[2];
	}

	bool IsLive(const UE::UObject* a_o)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_o || !arr) return false;
		const std::int32_t idx = a_o->internalIndex;
		if (idx < 0 || idx >= arr->GetObjectArrayNum()) return false;
		auto* item = arr->IndexToObject(idx);
		return item && reinterpret_cast<const UE::UObject*>(item->object) == a_o;
	}

	UE::UFunction* FindFunction(UE::UClass* a_class, const wchar_t* a_name)
	{
		if (!a_class) return nullptr;
		return a_class->FindFunctionByName(UE::FName(a_name, UE::EFindName::Find));
	}

	std::optional<bool> CallBool(UE::UObject* a_obj, UE::UFunction* a_fn)
	{
		// Only a function whose whole frame is its bool return value: one parameter (the return), inside the buffer.
		if (!a_obj || !a_fn || a_fn->numParms != 1 || a_fn->parmsSize == 0 || a_fn->parmsSize > 16 || a_fn->returnValueOffset >= a_fn->parmsSize) {
			return std::nullopt;
		}
		alignas(16) std::uint8_t params[16]{};
		a_obj->ProcessEvent(a_fn, params);
		return params[a_fn->returnValueOffset] != 0;
	}

	std::size_t CachedCount()
	{
		std::scoped_lock l(g_lock);
		return g_cache.size();
	}
}
