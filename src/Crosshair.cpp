#include "Crosshair.h"

#include "Game.h"
#include "Settings.h"
#include "Ue.h"

namespace crosshair
{
	namespace
	{
		constexpr const wchar_t* kHolderClass = L"/Game/UI/Modern/HUD/Reticle/WBP_ModernHud_CrosshairSneakEye.WBP_ModernHud_CrosshairSneakEye_C";
		constexpr const char*    kImageName = "Crosshair";

		ue::Handle g_holder, g_image;   // kept across frames: checked by their object-array slots
		ULONGLONG  g_nextFind = 0;
		bool       g_controlling = false;   // this mod has changed the image's opacity since the game last had it
		float      g_gameOpacity = 1.0f;    // what the game had when this mod took over, put back on release
		float      g_now = 1.0f;            // the opacity this mod is fading
		ULONGLONG  g_lastTick = 0;
		ULONGLONG  g_aimUntil = 0;
		std::uint32_t g_lastCasts = 0;

		std::mutex  g_lock;   // the state for State()
		std::string g_status = "not found yet";
		std::string g_why;
		float       g_target = 1.0f;

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) {
				logger::info("crosshair: {}", a_s);
			}
			g_status = std::move(a_s);
		}

		UE::UObject* ObjProp(UE::UObject* a_obj, const char* a_name)
		{
			auto* cls = a_obj ? a_obj->GetClass() : nullptr;
			auto** p = cls ? ue::At<UE::UObject*>(a_obj, ue::Offset(cls, a_name)) : nullptr;
			return p ? *p : nullptr;
		}

		struct RawArray
		{
			UE::UObject** data;
			std::int32_t  num;
			std::int32_t  max;
		};

		// the widget named a_name in a user widget's tree (panels walked through their Slots), or nullptr
		UE::UObject* FindNamed(UE::UObject* a_widget, std::string_view a_name, int a_depth = 0)
		{
			if (!a_widget || a_depth > 16) {
				return nullptr;
			}
			if (ue::NameOf(a_widget) == a_name) {
				return a_widget;
			}
			auto* cls = a_widget->GetClass();
			static auto* userWidget = ue::Class(L"/Script/UMG.UserWidget");
			static auto* panel = ue::Class(L"/Script/UMG.PanelWidget");
			if (!cls) {
				return nullptr;
			}
			if (userWidget && cls->IsChildOf(userWidget)) {
				if (auto* hit = FindNamed(ObjProp(ObjProp(a_widget, "WidgetTree"), "RootWidget"), a_name, a_depth + 1)) {
					return hit;
				}
			}
			if (panel && cls->IsChildOf(panel)) {
				auto* slots = ue::At<RawArray>(a_widget, ue::Offset(cls, "Slots"));
				for (std::int32_t i = 0; slots && slots->data && i < slots->num && i < 512; ++i) {
					if (auto* hit = FindNamed(ObjProp(slots->data[i], "Content"), a_name, a_depth + 1)) {
						return hit;
					}
				}
			}
			return nullptr;
		}

		UE::UObject* Image()
		{
			if (auto* img = g_image.Get()) {
				return img;
			}
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextFind) {
				return nullptr;
			}
			g_nextFind = now + 2000;   // the HUD is built late and rebuilt on a load: looked for again every 2 s (rule 17)
			g_controlling = false;
			auto* holder = g_holder.Get();
			if (!holder) {
				holder = ue::FirstOf(ue::Class(kHolderClass));
				g_holder.Set(holder);
			}
			if (!holder) {
				Status("not found yet (the HUD's crosshair holder WBP_ModernHud_CrosshairSneakEye is not built)");
				return nullptr;
			}
			auto* img = FindNamed(holder, kImageName);
			if (!img) {
				Status(std::format("the holder {} has no widget named {} - the crosshair is left alone", ue::NameOf(holder), kImageName));
				return nullptr;
			}
			g_image.Set(img);
			Status(std::format("found: {} in {}", ue::NameOf(img), ue::NameOf(holder)));
			return img;
		}

		float Opacity(UE::UObject* a_w)
		{
			static auto* widget = ue::Class(L"/Script/UMG.Widget");
			const auto* o = widget ? ue::At<float>(a_w, ue::Offset(widget, "RenderOpacity")) : nullptr;
			return o ? *o : 1.0f;
		}

		void SetOpacity(UE::UObject* a_w, float a_o)
		{
			ue::Call c(a_w, L"SetRenderOpacity");
			c.Set("InOpacity", a_o);
			c.Run();
		}

		// give the crosshair back to the game: its own opacity put back once
		void Release(UE::UObject* a_img, const char* a_why)
		{
			if (g_controlling && a_img) {
				SetOpacity(a_img, g_gameOpacity);
				logger::debug("crosshair: back to the game ({})", a_why);
			}
			g_controlling = false;
			std::scoped_lock l(g_lock);
			g_why = a_why;
			g_target = g_gameOpacity;
		}
	}

	void Tick()
	{
		const auto& s = settings::Get();
		auto*       img = Image();
		if (!img) {
			return;
		}
		auto*      im = RE::InterfaceManager::GetInstance(false, false);
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const bool gameplay = im && player && im->menuMode == 1;   // Oblivion Remastered: 1 is gameplay
		if (s.xhMode == 0 || !gameplay) {
			Release(img, s.xhMode == 0 ? "the game's crosshair (iMode 0)" : "a menu, dialogue or a load");
			return;
		}

		// what the crosshair means right now
		const ULONGLONG now = GetTickCount64();
		const auto      g = game::Status();
		if (g.castEvents != g_lastCasts) {
			g_lastCasts = g.castEvents;
			g_aimUntil = now + static_cast<ULONGLONG>(std::max(0.2f, s.spellTurnSeconds) * 1000.0f);
		}
		const bool bow = g.cameraTag.find("DrawingWeapon") != std::string::npos || g.cameraTag.find("Aim") != std::string::npos;
		const bool aiming = bow || now < g_aimUntil;
		const bool target = im->activateRef != nullptr;
		const bool firstPerson = !player->is3rdPerson;
		const char* why = "nothing to aim at";
		bool        show = false;
		if (s.xhMode == 2) {
			why = "always hidden (iMode 2)";
		} else if (firstPerson && s.xhInFirstPerson) {
			show = true, why = "first person";
		} else if (aiming && s.xhWhenAiming) {
			show = true, why = bow ? "a bow drawn" : "a spell being cast";
		} else if (target && s.xhWhenTarget) {
			show = true, why = "something to activate";
		} else if (g.combatStance && s.xhWhenWeaponDrawn) {
			show = true, why = "a weapon drawn";
		}

		if (!g_controlling) {
			g_gameOpacity = std::max(Opacity(img), 0.01f) >= 0.99f ? 1.0f : Opacity(img);   // what the game had, put back on release
			g_now = Opacity(img);
			g_controlling = true;
		}
		const float target01 = show ? g_gameOpacity : 0.0f;
		const float dt = g_lastTick ? std::min(0.1f, static_cast<float>(now - g_lastTick) / 1000.0f) : 0.0f;
		g_lastTick = now;
		const float step = s.xhFadeSeconds > 0.001f ? dt / s.xhFadeSeconds : 1.0f;
		g_now = g_now < target01 ? std::min(target01, g_now + step) : std::max(target01, g_now - step);
		if (std::abs(Opacity(img) - g_now) > 0.01f) {
			SetOpacity(img, g_now);   // also puts it back if the game set its own value meanwhile
		}
		std::scoped_lock l(g_lock);
		if (g_why != why) {
			logger::debug("crosshair: {} ({})", show ? "shown" : "hidden", why);
		}
		g_why = why;
		g_target = target01;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "why", g_why }, { "target_opacity", g_target }, { "opacity", g_now }, { "controlling", g_controlling } };
	}
}
