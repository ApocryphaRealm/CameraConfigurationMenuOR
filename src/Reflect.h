#pragma once

// A small, cached reflection layer: find a property by name (or a dotted path into structs) on a UObject's class and
// read or write it. GAME THREAD ONLY (every caller is the ProcessEvent detour).
//
// CommonLibOB64 declares UE::FField but not FProperty and its subclasses. These are UE 5.3's offsets, the same table
// TestBench's UEReflect.h keeps; TestBench read and wrote through them in game on 2026-09-26 and 2026-09-28 (the CCM
// probes). Each resolved property is checked against its owner's size and its reflected type before it is used.

namespace reflect
{
	namespace Layout
	{
		inline constexpr std::size_t kElementSize = 0x34;   // FProperty::ElementSize   int32
		inline constexpr std::size_t kOffset = 0x44;        // FProperty::Offset_Internal int32
		inline constexpr std::size_t kSubclass = 0x70;      // FBoolProperty FieldSize/ByteOffset/ByteMask/FieldMask,
		                                                    // FStructProperty::Struct
	}

	enum class Kind : std::uint8_t { kNone, kBool, kFloat, kDouble, kInt, kObject, kVector, kRotator, kName, kStruct, kOther };

	struct Prop
	{
		std::int32_t offset = -1;       // from the start of the OBJECT, through every struct on the path
		std::int32_t size = 0;
		Kind         kind = Kind::kNone;
		std::uint8_t boolByte = 0;      // FBoolProperty ByteOffset
		std::uint8_t boolMask = 0xFF;   // FBoolProperty FieldMask
		bool Ok() const { return offset >= 0 && kind != Kind::kNone; }
	};

	std::string Utf8(const UE::FString& a_s);
	std::string Name(const UE::FName& a_n);
	std::string ClassName(const UE::UObject* a_o);

	// "Prop" or "Struct.Field.Field". Cached per (class, path); a miss is cached too, and logged once at warn.
	const Prop& Find(const UE::UObject* a_obj, std::string_view a_path);

	bool        GetBool(UE::UObject* a_obj, const Prop& a_p);
	void        SetBool(UE::UObject* a_obj, const Prop& a_p, bool a_v);
	float       GetFloat(UE::UObject* a_obj, const Prop& a_p);
	void        SetFloat(UE::UObject* a_obj, const Prop& a_p, float a_v);
	UE::UObject* GetObject(UE::UObject* a_obj, const Prop& a_p);   // nullptr unless it is a live object
	std::string GetName(UE::UObject* a_obj, const Prop& a_p);       // an FName property as text
	std::array<double, 3> GetVec(UE::UObject* a_obj, const Prop& a_p);   // FVector (x,y,z) / FRotator (pitch,yaw,roll)
	void        SetVec(UE::UObject* a_obj, const Prop& a_p, const std::array<double, 3>& a_v);

	bool IsLive(const UE::UObject* a_o);   // listed by the object array at its own index
	UE::UFunction* FindFunction(UE::UClass* a_class, const wchar_t* a_name);

	// Call a parameterless function that returns bool (IsAttacking) through ProcessEvent.
	std::optional<bool> CallBool(UE::UObject* a_obj, UE::UFunction* a_fn);

	std::size_t CachedCount();
}
