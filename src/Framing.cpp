#include "Framing.h"

#include "Reflect.h"
#include "Settings.h"

namespace framing
{
	namespace
	{
		using namespace reflect;

		constexpr double kPi = 3.14159265358979323846;

		// A float field of CurrentCameraSettingData: the game's value is the base, taken again whenever the field holds
		// something CCM did not write last (the game set a new state or zoom).
		struct Field
		{
			const char* path;
			bool        written = false;
			double      base = 0.0, last = 0.0;
			std::uint32_t rebases = 0;
			bool        missingLogged = false;

			// writes a_fn(base); returns false when the field does not exist on this build (logged once - rule 30)
			template <class F>
			bool Apply(UE::UObject* a_mgr, F&& a_fn)
			{
				const auto& p = Find(a_mgr, path);
				if (!p.Ok()) {
					if (!missingLogged) {
						missingLogged = true;
						logger::warn("framing: {} not found on the camera manager - that setting does nothing on this game build", path);
					}
					return false;
				}
				const double cur = GetFloat(a_mgr, p);
				if (!written || std::abs(cur - last) > 0.01) {
					if (written) ++rebases;
					base = cur;
				}
				const double target = a_fn(base);
				if (std::abs(target - cur) > 0.001) SetFloat(a_mgr, p, static_cast<float>(target));
				last = target;
				written = true;
				return true;
			}
		};

		Field g_arm{ "CurrentCameraSettingData.DesiredArmLength" };
		Field g_lag{ "CurrentCameraSettingData.PositionLagSpeed" };
		Field g_lagMax{ "CurrentCameraSettingData.CameraLagMaxDistance" };
		Field g_rotPitch{ "CurrentCameraSettingData.RotationLagSpeedPitch" };
		Field g_rotYaw{ "CurrentCameraSettingData.RotationLagSpeedYaw" };
		Field g_blend{ "CurrentCameraSettingData.TransitionDuration" };

		constexpr const char* kSocket = "CurrentCameraSettingData.DesiredSocketOffset";
		bool                  g_socketWritten = false;
		std::array<double, 3> g_socketBase{}, g_socketLast{};
		std::uint32_t         g_socketRebases = 0;

		// the eased offset: side, height, distance, mirror (+1 right shoulder, -1 left - eased too, so a shoulder swap
		// slides the camera across behind the head)
		using Offset = std::array<double, 4>;
		Offset g_now{ 0, 0, 0, 1 }, g_from{ 0, 0, 0, 1 }, g_to{ 0, 0, 0, 1 };
		double g_t = 1.0;

		// runaway guard: the game re-writing a field every frame would make CCM add its offset to its own last write
		std::uint32_t g_rebasesThisSecond = 0;
		double        g_secondClock = 0.0;
		bool          g_runawayLogged = false;

		std::mutex  g_lock;
		json        g_state = json::object();
		std::atomic<std::int32_t> g_current{ 0 };
		framing::Group g_lastLogged = framing::Group::kCount;

		bool Has(const std::string& a_tag, std::string_view a_part) { return a_tag.find(a_part) != std::string::npos; }

		struct Signals
		{
			bool   sneaking = false, sprinting = false, swimming = false, aiming = false, horseback = false, weaponDrawn = false;
			double speed = 0.0;   // horizontal, cm/s
		};

		// what the player is doing, from the signals Ultimate Combat Redux already relies on (Context.lua / Dodge.lua) and
		// the camera manager's own state tag
		Signals Read(const framing::Inputs& a_in)
		{
			Signals g;
			g.weaponDrawn = a_in.weaponDrawn;
			const auto& tag = a_in.cameraTag;
			g.aiming = Has(tag, "Aiming");
			g.horseback = Has(tag, "Horse") || Has(tag, "Mount") || Has(tag, "Riding");
			g.sprinting = Has(tag, "Sprint");
			g.swimming = Has(tag, "Swim");
			g.sneaking = Has(tag, "Sneak");
			auto* pawn = a_in.pawn;
			if (!pawn) return g;
			if (auto* st = GetObject(pawn, Find(pawn, "OblivionActorStatePairingComponent"))) {
				g.sneaking = g.sneaking || GetBool(st, Find(st, "bIsSneaking"));
			}
			if (auto* mv = GetObject(pawn, Find(pawn, "PairedPawnMovementComponent"))) {
				static UE::UClass*    s_class = nullptr;
				static UE::UFunction* s_sprint = nullptr;
				static UE::UFunction* s_swim = nullptr;
				auto* cls = mv->GetClass();
				if (cls && cls != s_class) {
					s_class = cls;
					s_sprint = FindFunction(cls, L"IsSprinting");
					s_swim = FindFunction(cls, L"IsSwimming");
					logger::info("framing: the paired movement component is {} - IsSprinting {:p}, IsSwimming {:p}", ClassName(mv),
						static_cast<void*>(s_sprint), static_cast<void*>(s_swim));
				}
				g.sprinting = g.sprinting || CallBool(mv, s_sprint).value_or(false);
				g.swimming = g.swimming || CallBool(mv, s_swim).value_or(false);
			}
			if (auto* move = a_in.movement) {
				const auto& vp = Find(move, "Velocity");
				if (vp.Ok()) {
					const auto v = GetVec(move, vp);
					g.speed = std::hypot(v[0], v[1]);
				}
			}
			return g;
		}

		framing::Group Pick(const Signals& g)
		{
			using G = framing::Group;
			if (g.aiming) return G::kBowAiming;
			if (g.horseback) return G::kHorseback;
			if (g.swimming) return G::kSwimming;
			if (g.sneaking) return G::kSneaking;
			if (g.sprinting) return G::kSprinting;
			if (g.weaponDrawn) return G::kWeaponDrawn;
			if (g.speed > 20.0) return G::kMoving;
			return G::kStanding;
		}

		bool Near(const std::array<double, 3>& a, const std::array<double, 3>& b)
		{
			return std::abs(a[0] - b[0]) < 0.01 && std::abs(a[1] - b[1]) < 0.01 && std::abs(a[2] - b[2]) < 0.01;
		}

		bool SameOffset(const Offset& a, const Offset& b)
		{
			for (std::size_t i = 0; i < 4; ++i)
				if (std::abs(a[i] - b[i]) > 0.001) return false;
			return true;
		}

		// the value a smoothing setting asks for: -1 is the game's own
		double OrGame(float a_setting, double a_base) { return a_setting < 0.0f ? a_base : static_cast<double>(a_setting); }
	}

	const char* GroupKey(Group a_g)
	{
		switch (a_g) {
		case Group::kMoving: return "Moving";
		case Group::kSprinting: return "Sprinting";
		case Group::kSneaking: return "Sneaking";
		case Group::kWeaponDrawn: return "WeaponDrawn";
		case Group::kBowAiming: return "BowAiming";
		case Group::kSwimming: return "Swimming";
		case Group::kHorseback: return "Horseback";
		default: return "Standing";
		}
	}

	Group Current() { return static_cast<Group>(g_current.load(std::memory_order_relaxed)); }

	double Ease(int a_curve, double t)
	{
		t = std::clamp(t, 0.0, 1.0);
		const auto inOut = [t](auto in) { return t < 0.5 ? in(2.0 * t) * 0.5 : 1.0 - in(2.0 - 2.0 * t) * 0.5; };
		const auto out = [t](auto in) { return 1.0 - in(1.0 - t); };
		const auto pw = [](double n) { return [n](double x) { return std::pow(x, n); }; };
		const auto sine = [](double x) { return 1.0 - std::cos(x * kPi / 2.0); };
		const auto circ = [](double x) { return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x)); };
		const auto expo = [](double x) { return x <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * x - 10.0); };
		switch (a_curve) {
		case 1: return pw(2)(t);
		case 2: return out(pw(2));
		case 3: return inOut(pw(2));
		case 4: return pw(3)(t);
		case 5: return out(pw(3));
		case 6: return inOut(pw(3));
		case 7: return pw(4)(t);
		case 8: return out(pw(4));
		case 9: return inOut(pw(4));
		case 10: return pw(5)(t);
		case 11: return out(pw(5));
		case 12: return inOut(pw(5));
		case 13: return sine(t);
		case 14: return out(sine);
		case 15: return inOut(sine);
		case 16: return circ(t);
		case 17: return out(circ);
		case 18: return inOut(circ);
		case 19: return t >= 1.0 ? 1.0 : expo(t);
		case 20: return t >= 1.0 ? 1.0 : out(expo);
		case 21: return t <= 0.0 ? 0.0 : (t >= 1.0 ? 1.0 : inOut(expo));
		default: return t;   // 0 = linear
		}
	}

	void Apply(const Inputs& a_in)
	{
		auto* a_mgr = a_in.manager;
		if (!a_mgr) return;
		const bool   a_enabled = a_in.enabled;
		const double a_dt = a_in.dt;
		const auto&  s = settings::Get();

		// ---- the context, and the position it asks for (Standing's when it has none of its own) ----
		const Signals sig = Read(a_in);
		const Group   group = Pick(sig);
		g_current.store(static_cast<std::int32_t>(group), std::memory_order_relaxed);
		if (group != g_lastLogged) {
			logger::debug("framing: context {} -> {}", g_lastLogged == Group::kCount ? "(none)" : GroupKey(g_lastLogged), GroupKey(group));
			g_lastLogged = group;
		}
		const auto  gi = static_cast<std::size_t>(group);
		const bool  own = gi != 0 && gi < s.fmGroups.size() && s.fmGroups[gi].own;
		const auto& pos = s.fmGroups[own ? gi : 0];
		Offset want{ 0, 0, 0, 1 };
		const char* set = "the game's";
		if (a_enabled) {
			want = Offset{ double(pos.side), double(pos.height), double(pos.distance), a_in.shoulderLeft ? -1.0 : 1.0 };
			set = own ? GroupKey(group) : "Standing";
		}
		if (!SameOffset(want, g_to)) {
			g_from = g_now;
			g_to = want;
			g_t = 0.0;
		}
		if (!s.smEaseOffsets || s.smOffsetSeconds <= 0.0f) {
			g_t = 1.0;
		} else {
			g_t = std::min(1.0, g_t + a_dt / s.smOffsetSeconds);
		}
		const double k = Ease(s.smOffsetEasing, g_t);
		for (std::size_t i = 0; i < 4; ++i) g_now[i] = g_from[i] + (g_to[i] - g_from[i]) * k;

		// ---- the socket offset: base + side / height, the whole Y mirrored ----
		std::array<double, 3> sockNow{};
		const auto& p = Find(a_mgr, kSocket);
		if (p.Ok()) {
			sockNow = GetVec(a_mgr, p);
			if (!g_socketWritten || !Near(sockNow, g_socketLast)) {
				if (g_socketWritten) {
					++g_socketRebases;
					++g_rebasesThisSecond;
				}
				g_socketBase = sockNow;
			}
			std::array<double, 3> target = g_socketBase;
			target[1] = (g_socketBase[1] + g_now[0]) * g_now[3];
			target[2] = g_socketBase[2] + g_now[1];
			if (!Near(target, sockNow)) SetVec(a_mgr, p, target);
			g_socketLast = target;
			g_socketWritten = true;
		}

		// ---- distance and smoothing ----
		const std::uint32_t armBefore = g_arm.rebases;
		g_arm.Apply(a_mgr, [&](double b) { return std::max(20.0, b + g_now[2]); });
		g_rebasesThisSecond += g_arm.rebases - armBefore;
		g_lag.Apply(a_mgr, [&](double b) { return a_enabled ? OrGame(s.smFollowSpeed, b) : b; });
		g_lagMax.Apply(a_mgr, [&](double b) { return a_enabled ? OrGame(s.smMaxLagDistance, b) : b; });
		g_rotPitch.Apply(a_mgr, [&](double b) { return a_enabled ? OrGame(s.smRotationPitch, b) : b; });
		g_rotYaw.Apply(a_mgr, [&](double b) { return a_enabled ? OrGame(s.smRotationYaw, b) : b; });
		g_blend.Apply(a_mgr, [&](double b) { return a_enabled ? OrGame(s.smStateBlendSeconds, b) : b; });

		// ---- runaway guard: re-bases every frame mean the game rewrites the field continuously ----
		g_secondClock += a_dt;
		if (g_secondClock >= 1.0) {
			if (g_rebasesThisSecond > 10 && !g_runawayLogged) {
				g_runawayLogged = true;
				logger::warn("framing: the game re-wrote the camera offset {} times in a second - it may be easing the setting data itself, so CCM's offsets could stack. Report this with the log.", g_rebasesThisSecond);
			}
			g_rebasesThisSecond = 0;
			g_secondClock = 0.0;
		}

		std::scoped_lock l(g_lock);
		g_state = json{
			{ "context", GroupKey(group) }, { "offsets_from", set },
			{ "signals", { { "sneaking", sig.sneaking }, { "sprinting", sig.sprinting }, { "swimming", sig.swimming }, { "bow_aiming", sig.aiming },
				{ "horseback", sig.horseback }, { "weapon_drawn", sig.weaponDrawn }, { "speed", sig.speed }, { "camera_tag", a_in.cameraTag } } },
			{ "eased", { { "side", g_now[0] }, { "height", g_now[1] }, { "distance", g_now[2] }, { "mirror", g_now[3] }, { "progress", g_t } } },
			{ "socket_base", g_socketBase }, { "socket_now", g_socketLast }, { "socket_rebases", g_socketRebases },
			{ "arm_base", g_arm.base }, { "arm_now", g_arm.last }, { "arm_rebases", g_arm.rebases },
			{ "follow_speed", { { "game", g_lag.base }, { "now", g_lag.last } } },
			{ "max_lag_distance", { { "game", g_lagMax.base }, { "now", g_lagMax.last } } },
			{ "rotation_pitch", { { "game", g_rotPitch.base }, { "now", g_rotPitch.last } } },
			{ "rotation_yaw", { { "game", g_rotYaw.base }, { "now", g_rotYaw.last } } },
			{ "state_blend", g_blend.missingLogged ? json("not on this build") : json{ { "game", g_blend.base }, { "now", g_blend.last } } },
		};
	}

	std::array<double, 3> SocketBase() { return g_socketBase; }

	json State()
	{
		std::scoped_lock l(g_lock);
		return g_state;
	}
}
