#include "Marker.h"

#include "Aim.h"
#include "Ue.h"

namespace marker
{
	namespace
	{
		constexpr const wchar_t* kTextClass = L"/Game/UI/Modern/Prefabs/WBP_AltarTextBlock.WBP_AltarTextBlock_C";   // the game's text prefab (Tween Menu's labels)
		// Oblivion Remastered's full names are localisation KEYS ("LOC_FN_Arrow1Iron") into this string table (the packaged
		// asset Content/Localization/StringTables/ST_FullNames); the game's UI shows the table's text, and so does the marker
		constexpr const wchar_t* kFullNames = L"/Game/Localization/StringTables/ST_FullNames.ST_FullNames";
		constexpr double         kScale = 0.8;   // the prefab's size is a little large over the world (the owner, 2026-09-29)

		ue::Handle   g_root, g_label, g_slot;   // kept across frames: checked by their object-array slots
		bool         g_shown = false;
		bool         g_locking = false;   // the lock-on owns the marker
		bool         g_warm = false;      // the label's colour is the lock's
		std::wstring g_text;
		double       g_x = -1, g_y = -1;
		ULONGLONG    g_lastBuild = 0;

		std::mutex  g_lock;   // the state for State()
		std::string g_status = "not built";
		std::string g_marking;

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) {
				logger::info("marker: {}", a_s);
			}
			g_status = std::move(a_s);
		}

		// the first parameter's offset (Blueprint and native setters name theirs freely): FField chain head, Offset_Internal
		std::int32_t FirstParam(UE::UFunction* a_fn)
		{
			auto* st = reinterpret_cast<UE::UStruct*>(a_fn);
			auto* f = st ? reinterpret_cast<std::uint8_t*>(st->childProperties) : nullptr;
			return f ? *reinterpret_cast<const std::int32_t*>(f + 0x44) : -1;
		}

		// calls a_fn with a_bytes as its first parameter
		bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
		{
			auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
			const auto off = fn ? FirstParam(fn) : -1;
			if (off < 0) {
				return false;
			}
			std::vector<std::uint8_t> params(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize) + 16, 0);
			std::memcpy(params.data() + off, a_bytes, a_size);
			a_obj->ProcessEvent(fn, params.data());
			return true;
		}

		// white with a soft dark shadow (the prefab's style is black). The prefab is a CommonUI text block: its text style
		// is applied when its Slate widget is constructed (AddToViewport) and overwrites a colour set before that - the
		// first build set it before and stayed black - so this runs after AddToViewport and again with every new text.
		void ApplyColour(UE::UObject* a_label)
		{
			struct SlateColor
			{
				float        rgba[4];
				std::uint8_t rule;   // ESlateColorStylingMode: 0 = the colour given
				std::uint8_t pad[7];
			} white{ { 1.0f, 1.0f, 1.0f, 1.0f }, 0, {} };
			if (g_warm) white = { { 1.0f, 0.62f, 0.28f, 1.0f }, 0, {} };   // the lock-on's target: a warm amber
			// the prefab's own SetColor (what Tween Menu colours these labels with, proven in game) and the text block's
			const bool bp = CallFirst(a_label, L"SetColor", &white, sizeof(white));
			const bool native = CallFirst(a_label, L"SetColorAndOpacity", &white, sizeof(white));
			static bool logged = false;
			if (!logged) {
				logged = true;
				logger::info("marker: colour set through {}{}", bp ? "the prefab's SetColor" : "(no SetColor on the prefab)",
					native ? " and SetColorAndOpacity" : " (no SetColorAndOpacity)");
			}
			const float shadow[4] = { 0.0f, 0.0f, 0.0f, 0.85f };
			CallFirst(a_label, L"SetShadowColorAndOpacity", shadow, sizeof(shadow));
			const double offset[2] = { 1.5, 1.5 };
			CallFirst(a_label, L"SetShadowOffset", offset, sizeof(offset));
		}

		UE::UObject* Create(const wchar_t* a_classPath)
		{
			static auto* lib = ue::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			auto* cls = ue::Class(a_classPath);
			auto* pc = aim::PlayerController();
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!cdo || !cls || !pc) {
				return nullptr;
			}
			ue::Call c(cdo, L"Create");
			c.Set("WorldContextObject", pc);
			c.Set("WidgetType", cls);
			c.Set("OwningPlayer", pc);
			if (!c.RunGuarded()) return nullptr;   // a world-context call: fault-guarded (a quit, a load)
			return c.Get<UE::UObject*>("ReturnValue");
		}

		void SetVisible(bool a_visible)
		{
			if (auto* root = g_root.Get(); root && a_visible != g_shown) {
				ue::Call c(root, L"SetVisibility");
				c.Set("InVisibility", static_cast<std::uint8_t>(a_visible ? 3 : 1));   // HitTestInvisible (drawn, never takes the mouse) / Collapsed
				c.Run();
				g_shown = a_visible;
			}
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuild && now - g_lastBuild < 2000) {
				return false;   // at most every 2 s (rule 17: the player controller may not exist yet)
			}
			g_lastBuild = now;
			auto* root = Create(L"/Script/UMG.UserWidget");
			auto* treeClass = ue::Class(L"/Script/UMG.WidgetTree");
			auto* canvasClass = ue::Class(L"/Script/UMG.CanvasPanel");
			auto* label = Create(kTextClass);
			if (!root || !treeClass || !canvasClass || !label) {
				Status("cannot be built yet (no player controller, or the game's text prefab is missing)");
				return false;
			}
			auto** tree = root->GetClass() ? ue::At<UE::UObject*>(root, ue::Offset(root->GetClass(), "WidgetTree")) : nullptr;
			if (tree && !*tree) {
				*tree = UE::NewObject<UE::UObject>(root, treeClass, UE::FName(L"BTPSMarkerTree"));
			}
			auto* canvas = tree && *tree ? UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName(L"BTPSMarkerCanvas")) : nullptr;
			auto** rootWidget = tree && *tree ? ue::At<UE::UObject*>(*tree, ue::Offset(treeClass, "RootWidget")) : nullptr;
			if (!canvas || !rootWidget) {
				Status("its canvas could not be made");
				return false;
			}
			*rootWidget = canvas;
			ue::Call add(canvas, L"AddChildToCanvas");
			add.Set("Content", label);
			add.Run();
			auto* slot = add.Get<UE::UObject*>("ReturnValue");
			if (!slot) {
				Status("the canvas refused the text");
				return false;
			}
			const double anchors[4] = { 0.0, 0.0, 0.0, 0.0 };   // top left: the position is a viewport position
			CallFirst(slot, L"SetAnchors", anchors, sizeof(anchors));
			const double align[2] = { 0.5, 1.0 };               // centred, standing on the point
			CallFirst(slot, L"SetAlignment", align, sizeof(align));
			const bool yes = true;
			CallFirst(slot, L"SetAutoSize", &yes, sizeof(yes));
			ue::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 40);
			vp.Run();
			ApplyColour(label);   // after the Slate widget exists: the style applied at construction is already in
			// a little smaller than the prefab (the render scale is not part of the text style, so it holds from here)
			const double scale[2] = { kScale, kScale };
			CallFirst(label, L"SetRenderScale", scale, sizeof(scale));
			g_root.Set(root);
			g_label.Set(label);
			g_slot.Set(slot);
			g_shown = true;   // SetVisible below hides it (a new widget is visible)
			g_text.clear();
			g_x = g_y = -1;
			SetVisible(false);
			Status("built (the game's text prefab on a canvas, on the viewport)");
			return true;
		}

		void SetText(const std::wstring& a_text)
		{
			auto* label = g_label.Get();
			if (!label || a_text == g_text) {
				return;
			}
			auto* fn = label->FindFunction(UE::FName(L"SetText", UE::EFindName::Find));
			const auto off = fn ? FirstParam(fn) : -1;
			if (off < 0) {
				Status("the text has no SetText - the name cannot change");
				return;
			}
			std::vector<std::uint8_t> params(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(fn)->propertiesSize) + 16, 0);
			UE::FText* text = nullptr;
			if (a_text.starts_with(L"LOC_")) {
				// the table's text for the key (KismetTextLibrary::TextFromStringTable - what the game's own UI shows)
				static auto* lib = ue::Class(L"/Script/Engine.KismetTextLibrary");
				ue::Call     t(lib ? lib->GetDefaultObject(false) : nullptr, L"TextFromStringTable");
				void*        id = t.At("TableId");
				void*        key = t.At("Key");
				void*        ret = t.At("ReturnValue");
				if (id && key && ret) {
					new (id) UE::FName(kFullNames, UE::EFindName::Add);
					auto* k = new (key) UE::FString(a_text.c_str());
					t.Run();
					auto* got = static_cast<UE::FText*>(ret);
					text = new (params.data() + off) UE::FText(*got);   // shares the text's data; both are released below
					got->~FText();
					k->~FString();
				} else {
					static bool warned = false;
					if (!warned) {
						warned = true;
						logger::warn("marker: TextFromStringTable is missing - names show as their keys");
					}
				}
			}
			if (!text) {
				text = new (params.data() + off) UE::FText(UE::FText::AsCultureInvariant(UE::FString(a_text.c_str())));
			}
			label->ProcessEvent(fn, params.data());
			text->~FText();
			g_text = a_text;
			ApplyColour(label);   // with every new text, so a style re-applied by the prefab since cannot keep it black
		}

		// where the marker stands: above a person or creature's head, at the top of a container or door, just above an item
		RE::NiPoint3 Anchor(RE::TESObjectREFR* a_ref)
		{
			RE::NiPoint3 p = a_ref->data.location;
			const auto   type = a_ref->data.objectReference ? a_ref->data.objectReference->GetFormType() : RE::FormType::None;
			switch (type) {
			case RE::FormType::NPC:
			case RE::FormType::Creature:
				p.z += 140.0f;
				break;
			case RE::FormType::Container:
			case RE::FormType::Door:
			case RE::FormType::Activator:
			case RE::FormType::Furniture:
			case RE::FormType::Flora:
				p.z += 60.0f;
				break;
			default:
				p.z += 12.0f;
				break;
			}
			return p;
		}

		// the point's place on the viewport in widget units (UMG's own projection); false when it is behind the camera
		bool Project(const RE::NiPoint3& a_point, double& a_x, double& a_y)
		{
			UE::FVector world;
			if (!aim::ToUnreal(a_point, world)) {
				return false;
			}
			static auto* lib = ue::Class(L"/Script/UMG.WidgetLayoutLibrary");
			auto* pc = aim::PlayerController();
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!cdo || !pc) {
				return false;
			}
			ue::Call c(cdo, L"ProjectWorldLocationToWidgetPosition");
			if (!c) {
				Status("UMG has no ProjectWorldLocationToWidgetPosition - the marker cannot be placed");
				return false;
			}
			c.Set("PlayerController", pc);
			c.Set("WorldLocation", world);
			c.Set("bPlayerViewportRelative", false);
			c.Run();
			const auto pos = c.Get<std::array<double, 2>>("ScreenPosition");   // FVector2D: two doubles
			a_x = pos[0];
			a_y = pos[1];
			return c.Get<bool>("ReturnValue");
		}
	}

	namespace
	{
		void Mark(RE::TESObjectREFR* a_ref);
	}

	void Show(RE::TESObjectREFR* a_ref)
	{
		if (g_locking) return;   // the lock-on's target is marked instead
		Mark(a_ref);
	}

	void Lock(RE::TESObjectREFR* a_ref)
	{
		const bool warm = a_ref != nullptr;
		if (!warm && !g_locking) return;   // nothing of the lock's to take down: the selection keeps its marker
		g_locking = warm;
		if (warm != g_warm) {
			g_warm = warm;
			if (auto* label = g_label.Get()) ApplyColour(label);
		}
		Mark(a_ref);
	}

	namespace
	{
	void Mark(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			SetVisible(false);
			std::scoped_lock l(g_lock);
			g_marking.clear();
			return;
		}
		if (!g_root.Get() || !g_label.Get() || !g_slot.Get()) {
			if (!Build()) {
				return;
			}
		}
		double x = 0, y = 0;
		if (!Project(Anchor(a_ref), x, y)) {
			SetVisible(false);
			return;
		}
		auto*          base = a_ref->data.objectReference;
		const char*    n = base ? RE::TESFullName::GetFullName(base) : nullptr;
		std::wstring   name;
		if (n && *n) {
			const int len = ::MultiByteToWideChar(CP_UTF8, 0, n, -1, nullptr, 0);
			name.resize(len > 0 ? static_cast<std::size_t>(len - 1) : 0);
			if (len > 1) ::MultiByteToWideChar(CP_UTF8, 0, n, -1, name.data(), len);
		}
		SetText(name);
		if (std::abs(x - g_x) > 0.5 || std::abs(y - g_y) > 0.5) {
			const double pos[2] = { x, y };
			CallFirst(g_slot.Get(), L"SetPosition", pos, sizeof(pos));
			g_x = x;
			g_y = y;
		}
		SetVisible(true);
		std::scoped_lock l(g_lock);
		g_marking = std::format("{}{} at ({:.0f}, {:.0f})", g_locking ? "locked on: " : "", n && *n ? n : "(no name)", x, y);
	}
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "marking", g_marking }, { "shown", g_shown } };
	}
}
