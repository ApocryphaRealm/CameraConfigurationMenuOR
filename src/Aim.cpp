#include "Aim.h"

#include "Ue.h"

namespace aim
{
	namespace
	{
		struct Vec
		{
			double x = 0, y = 0, z = 0;
		};

		// One of eight arrangements of the horizontal axes: bit 0 mirrors Unreal's x, bit 1 mirrors its y, bit 2 swaps
		// x and y. Height is up on both sides.
		struct Mapping
		{
			int  index = 0;
			bool swap() const { return (index & 4) != 0; }
			double sx() const { return (index & 1) ? -1.0 : 1.0; }
			double sy() const { return (index & 2) ? -1.0 : 1.0; }

			Vec ToUe(const Vec& o) const
			{
				const double a0 = swap() ? o.y : o.x, a1 = swap() ? o.x : o.y;
				return { sx() * a0, sy() * a1, o.z };
			}
			Vec ToOblivion(const Vec& u) const
			{
				const double b0 = sx() * u.x, b1 = sy() * u.y;
				return swap() ? Vec{ b1, b0, u.z } : Vec{ b0, b1, u.z };
			}
			std::string Describe() const
			{
				const char* ox = swap() ? "obl.y" : "obl.x";
				const char* oy = swap() ? "obl.x" : "obl.y";
				return std::format("UE x = {}{}, UE y = {}{}", sx() < 0 ? "-" : "+", ox, sy() < 0 ? "-" : "+", oy);
			}
		};

		struct Calibration
		{
			std::array<double, 8> score{};   // running agreement per mapping (cosine of each step, slowly decayed)
			std::deque<double>    ratios;    // Unreal length / Oblivion length per step, the last 32
			int                   samples = 0;
			bool                  haveLast = false;
			Vec                   lastObl, lastUe;
			Mapping               mapping;
			double                scale = 0.0;
			double                fit = 0.0;   // the kept mapping's average agreement, 1 = perfect
			bool                  done = false;
		};

		struct Reading
		{
			bool        haveCamera = false;
			Vec         camLoc, camRot, pawnLoc;
			double      halfHeight = 0;
			Vec         playerLoc;
			float       playerHeading = 0, playerPitch = 0;
			View        view;
			std::string problem;
		};

		Calibration      g_cal;
		std::mutex       g_lock;   // g_last and the calibration summary, for State()
		Reading          g_last;
		std::atomic_bool g_calibrated{ false };

		Vec  Sub(const Vec& a, const Vec& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		double LenXY(const Vec& a) { return std::sqrt(a.x * a.x + a.y * a.y); }

		UE::UObject* FindPlayerController()
		{
			static ue::Handle cached;   // kept across frames: checked by its slot, never by reading it
			static ULONGLONG  lastScan = 0;
			if (auto* pc = cached.Get()) {
				return pc;
			}
			const ULONGLONG now = GetTickCount64();
			if (now - lastScan < 2000) {
				return nullptr;   // the object array scan is not cheap: at most every 2 s until found (rule 17)
			}
			lastScan = now;
			auto* found = ue::FirstOf(ue::Class(L"/Script/Engine.PlayerController"));
			cached.Set(found);
			static bool logged = false;
			if (found && !logged) {
				logged = true;
				logger::info("camera: player controller found ({})", ue::NameOf(found));
			}
			return found;
		}

		// an object held by a property of a_owner, by the property's name (the offset is learned once per class)
		UE::UObject* PropertyObject(UE::UObject* a_owner, const char* a_property, UE::UClass*& a_class, std::int32_t& a_offset)
		{
			if (!a_owner) {
				return nullptr;
			}
			auto* cls = a_owner->GetClass();
			if (!cls) {
				return nullptr;
			}
			if (cls != a_class) {
				a_class = cls;
				a_offset = ue::Offset(cls, a_property);
				logger::info("camera: {}.{} at 0x{:X}", ue::NameOf(cls), a_property, a_offset);
			}
			auto** p = ue::At<UE::UObject*>(a_owner, a_offset);
			auto*  o = p ? *p : nullptr;
			return o && ue::IsLive(o) ? o : nullptr;
		}

		void Learn(const Vec& a_obl, const Vec& a_ue)
		{
			auto& c = g_cal;
			if (!c.haveLast) {
				c.haveLast = true;
				c.lastObl = a_obl;
				c.lastUe = a_ue;
				return;
			}
			const Vec    dObl = Sub(a_obl, c.lastObl);
			const Vec    dUe = Sub(a_ue, c.lastUe);
			const double lObl = LenXY(dObl), lUe = LenXY(dUe);
			if (lObl < 48.0) {
				return;   // too short a step to tell the axes apart: wait for more movement
			}
			c.lastObl = a_obl;
			c.lastUe = a_ue;
			if (lObl > 1500.0 || lUe < 1.0) {
				logger::debug("camera: a jump of {:.0f} units (a load or a teleport) - not a calibration step", lObl);
				return;
			}
			for (int i = 0; i < 8; ++i) {
				const Vec m = Mapping{ i }.ToUe(dObl);
				const double cos = (m.x * dUe.x + m.y * dUe.y) / (LenXY(m) * lUe);
				c.score[static_cast<std::size_t>(i)] = c.score[static_cast<std::size_t>(i)] * 0.98 + cos;
			}
			c.ratios.push_back(lUe / lObl);
			if (c.ratios.size() > 32) {
				c.ratios.pop_front();
			}
			c.samples = std::min(c.samples + 1, 1000);

			const auto best = static_cast<int>(std::ranges::max_element(c.score) - c.score.begin());
			double weight = 0;
			for (int k = 0; k < std::min(c.samples, 200); ++k) weight += std::pow(0.98, k);
			const double fit = c.score[static_cast<std::size_t>(best)] / weight;
			std::vector<double> r(c.ratios.begin(), c.ratios.end());
			std::ranges::nth_element(r, r.begin() + static_cast<std::ptrdiff_t>(r.size() / 2));
			const double scale = r[r.size() / 2];
			const bool   ready = c.samples >= 5 && fit >= 0.9;
			if (ready && (!c.done || best != c.mapping.index)) {
				logger::info("camera: calibrated - {}, scale {:.4f} Unreal units per Oblivion unit ({} steps, agreement {:.3f})",
					Mapping{ best }.Describe(), scale, c.samples, fit);
			}
			if (!ready && c.done) {
				logger::warn("camera: the calibration no longer fits ({:.3f} over {} steps) - selection paused until it does", fit, c.samples);
			}
			c.done = ready;
			c.mapping = Mapping{ best };
			c.scale = scale;
			c.fit = fit;
			g_calibrated.store(ready);
		}

		json VecJson(const Vec& v) { return json::array({ std::round(v.x * 10) / 10, std::round(v.y * 10) / 10, std::round(v.z * 10) / 10 }); }
	}

	View Read(RE::PlayerCharacter* a_player)
	{
		static ue::Getter camLocation(L"GetCameraLocation"), camRotation(L"GetCameraRotation"), getPawn(L"K2_GetPawn"),
			actorLocation(L"K2_GetActorLocation"), halfHeight(L"GetScaledCapsuleHalfHeight");
		static UE::UClass* pcClass = nullptr;
		static std::int32_t pcCameraOffset = -1;
		static UE::UClass* pawnClass = nullptr;
		static std::int32_t pawnCapsuleOffset = -1;

		Reading r;
		const auto fail = [&](const char* a_why) {
			static std::string lastWhy;
			if (lastWhy != a_why) {
				lastWhy = a_why;
				logger::info("camera: not read - {}", a_why);   // once per change of the reason
			}
			r.problem = a_why;
			std::scoped_lock l(g_lock);
			g_last = r;
			return View{};
		};
		if (!a_player) {
			return fail("no player");
		}
		if (!ue::SelfCheck()) {
			return fail("Unreal's property layout is not proven yet");
		}
		auto* pc = FindPlayerController();
		if (!pc) {
			return fail("no player controller yet");
		}
		auto* cam = PropertyObject(pc, "PlayerCameraManager", pcClass, pcCameraOffset);
		UE::FVector   loc{};
		UE::FRotator  rot{};
		UE::UObject*  pawn = nullptr;
		UE::FVector   pawnLoc{};
		if (!cam || !camLocation.Get(cam, loc) || !camRotation.Get(cam, rot)) {
			return fail("no camera manager");
		}
		if (!getPawn.Get(pc, pawn) || !pawn || !ue::IsLive(pawn) || !actorLocation.Get(pawn, pawnLoc)) {
			return fail("no pawn");
		}
		float hh = 0.0f;
		if (auto* capsule = PropertyObject(pawn, "CapsuleComponent", pawnClass, pawnCapsuleOffset)) {
			halfHeight.Get(capsule, hh);
		}
		r.haveCamera = true;
		r.camLoc = { loc.x, loc.y, loc.z };
		r.camRot = { rot.pitch, rot.yaw, rot.roll };
		r.pawnLoc = { pawnLoc.x, pawnLoc.y, pawnLoc.z };
		r.halfHeight = hh;
		const auto& pl = a_player->data.location;
		r.playerLoc = { pl.x, pl.y, pl.z };
		r.playerHeading = a_player->data.angle.z;
		r.playerPitch = a_player->data.angle.x;

		static bool firstLogged = false;
		if (!firstLogged) {
			firstLogged = true;
			logger::info("camera: first reading - camera ({:.0f}, {:.0f}, {:.0f}) pitch {:.1f} yaw {:.1f}; pawn ({:.0f}, {:.0f}, {:.0f}) half height {:.0f}; "
						 "player reference ({:.0f}, {:.0f}, {:.0f}) heading {:.3f}",
				loc.x, loc.y, loc.z, rot.pitch, rot.yaw, pawnLoc.x, pawnLoc.y, pawnLoc.z, hh, pl.x, pl.y, pl.z, a_player->data.angle.z);
		}

		const Vec feetUe{ pawnLoc.x, pawnLoc.y, pawnLoc.z - hh };
		std::scoped_lock l(g_lock);   // the calibration is also read by State() on TestBench's thread
		Learn(r.playerLoc, feetUe);

		if (g_cal.done && g_cal.scale > 0.0) {
			const Vec    rel = g_cal.mapping.ToOblivion(Sub(r.camLoc, feetUe));
			const double p = rot.pitch * std::numbers::pi / 180.0, y = rot.yaw * std::numbers::pi / 180.0;
			const Vec    f = g_cal.mapping.ToOblivion({ std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p) });
			const double fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
			r.view.ok = fl > 0.5;
			r.view.eye = RE::NiPoint3(static_cast<float>(pl.x + rel.x / g_cal.scale), static_cast<float>(pl.y + rel.y / g_cal.scale),
				static_cast<float>(pl.z + rel.z / g_cal.scale));
			r.view.fwd = RE::NiPoint3(static_cast<float>(f.x / fl), static_cast<float>(f.y / fl), static_cast<float>(f.z / fl));
		} else {
			r.problem = "calibrating - the character has to walk a few steps";
		}
		g_last = r;
		return r.view;
	}

	bool Calibrated() { return g_calibrated.load(); }

	UE::UObject* PlayerController() { return FindPlayerController(); }

	bool ToUnreal(const RE::NiPoint3& a_oblivion, UE::FVector& a_out)
	{
		std::scoped_lock l(g_lock);
		const auto& r = g_last;
		if (!g_cal.done || g_cal.scale <= 0.0 || !r.haveCamera) {
			return false;
		}
		// the player's feet on both sides, and the offset to the point mapped and scaled across
		const Vec rel = g_cal.mapping.ToUe({ a_oblivion.x - r.playerLoc.x, a_oblivion.y - r.playerLoc.y, a_oblivion.z - r.playerLoc.z });
		a_out = UE::FVector(r.pawnLoc.x + rel.x * g_cal.scale, r.pawnLoc.y + rel.y * g_cal.scale, r.pawnLoc.z - r.halfHeight + rel.z * g_cal.scale);
		return true;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		const auto& r = g_last;
		json j{
			{ "ok", r.view.ok },
			{ "problem", r.problem },
			{ "calibration", { { "done", g_cal.done }, { "mapping", g_cal.mapping.Describe() }, { "scale", g_cal.scale }, { "agreement", g_cal.fit },
								 { "steps", g_cal.samples } } },
		};
		if (r.haveCamera) {
			j["ue"] = { { "camera", VecJson(r.camLoc) }, { "rotation_pyr", VecJson(r.camRot) }, { "pawn", VecJson(r.pawnLoc) },
				{ "half_height", r.halfHeight } };
			j["oblivion"] = { { "player", VecJson(r.playerLoc) }, { "heading", r.playerHeading }, { "pitch", r.playerPitch } };
		}
		if (r.view.ok) {
			j["view"] = { { "eye", VecJson({ r.view.eye.x, r.view.eye.y, r.view.eye.z }) },
				{ "forward", json::array({ r.view.fwd.x, r.view.fwd.y, r.view.fwd.z }) },
				{ "eye_above_feet", r.view.eye.z - r.playerLoc.z } };
		}
		return j;
	}
}
