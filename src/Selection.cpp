#include "Selection.h"

#include "Activate.h"
#include "Aim.h"
#include "Marker.h"
#include "Settings.h"

// CCM's [Selection] settings seen the way Better Third-Person Selection's code reads them (its code kept as finalized)
namespace sel
{
	enum ApplyTo : int
	{
		kObserve = 0,
		kPickRef = 1,
		kReticleRef = 2,
		kCrosshairRef = 3,
		kActivateRef = 4,   // the game's own prompt target: what CCM writes
		kAllFour = 5,
	};

	inline const char* ApplyToName(int a_applyTo)
	{
		static constexpr const char* names[]{ "observe", "pickRef", "reticleRef", "crosshairRef", "activateRef", "all four" };
		return a_applyTo >= 0 && a_applyTo <= kAllFour ? names[a_applyTo] : "?";
	}

	struct View
	{
		bool  enabled, thirdPerson, firstPerson, showMarker, logTargets;
		float range, maxAngle;
		int   applyTo;
	};

	inline View Snapshot()
	{
		const auto& v = settings::Get();
		return { v.selEnabled, v.selThirdPerson, v.selFirstPerson, v.selShowMarker, v.selLogTargets, v.selRange, v.selMaxAngle, kActivateRef };
	}
}

namespace selection
{
	namespace
	{
		struct Candidate
		{
			RE::TESObjectREFR* ref = nullptr;
			float              angle = 0;   // degrees from the camera's aim
			float              dist = 0;    // game units from the character
			float              score = 0;   // lower is better
		};

		std::mutex             g_lock;   // g_status and g_top, read by the page and the tool on other threads
		Status                 g_status;
		std::vector<json>      g_top;    // the best few candidates, described
		json                   g_gameFields = json::object();
		RE::TESObjectREFR*     g_written = nullptr;   // what this mod wrote last frame (nullptr: nothing)
		int                    g_writtenTo = sel::kObserve;
		std::atomic<std::uint32_t> g_cellsScanned{ 0 };
		std::uint64_t          g_writesKept = 0, g_writesReplaced = 0;   // the write check, summarised every 5 s
		ULONGLONG              g_writeSummaryAt = 0;

		// an Activate press on this mod's choice, waiting to see whether the game acted on it (by form ID: the reference
		// may be freed once taken, so it is looked up again, never dereferenced from here)
		struct PendingPress
		{
			RE::TESFormID id = 0;
			ULONGLONG     at = 0;
		} g_press;

		constexpr ULONGLONG kPressWaitMs = 200;

		// the reference by its form ID when it is still in the world (nullptr once taken, deleted, disabled or cell-less)
		RE::TESObjectREFR* StillThere(RE::TESFormID a_id)
		{
			auto* form = RE::TESForm::LookupByID(a_id);
			if (!form || (form->GetFormType() != RE::FormType::Reference && form->GetFormType() != RE::FormType::ActorCharacter &&
							 form->GetFormType() != RE::FormType::ActorCreature)) {
				return nullptr;
			}
			auto* ref = static_cast<RE::TESObjectREFR*>(form);
			if (ref->IsDeleted() || (ref->GetFormFlags() & RE::TESForm::RecordFlags::kDisabled) || !ref->parentCell) {
				return nullptr;
			}
			return ref;
		}

		constexpr float kCellSize = 4096.0f;
		constexpr float kBelowFeet = 100.0f;   // game units under the character's feet a reference may be
		constexpr float kAboveFeet = 250.0f;   // and over them (a top shelf, a tall person's head)

		std::string Name(RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return {};
			}
			auto*       base = a_ref->data.objectReference;
			const char* n = base ? RE::TESFullName::GetFullName(base) : nullptr;
			return std::format("{} [{:08X}]", n && *n ? n : "(no name)", a_ref->GetFormID());
		}

		// the reference's own kind when the player could use it; FormType::None when not
		RE::FormType UsableKind(RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_playerRef)
		{
			if (!a_ref || a_ref == a_playerRef || a_ref->IsDeleted() || (a_ref->GetFormFlags() & RE::TESForm::RecordFlags::kDisabled)) {
				return RE::FormType::None;
			}
			auto* base = a_ref->data.objectReference;
			if (!base) {
				return RE::FormType::None;
			}
			using enum RE::FormType;
			const auto type = base->GetFormType();
			switch (type) {
			case Apparatus:
			case Armor:
			case Book:
			case Clothing:
			case Ingredient:
			case Light:   // only a named light (a torch you can carry); fixtures carry no name
			case Misc:
			case Weapon:
			case Ammo:
			case SoulGem:
			case KeyMaster:
			case AlchemyItem:
			case SigilStone:
			case Container:
			case Door:
			case Activator:
			case Flora:
			case Furniture:
			case NPC:
			case Creature:
				break;
			default:
				return None;
			}
			const char* n = RE::TESFullName::GetFullName(base);
			return n && *n ? type : None;   // the game prompts only for what has a name
		}

		// the references in the cells around the character: the player's cell inside, the 3 x 3 around it outside
		std::vector<RE::TESObjectCELL*> NearbyCells(RE::PlayerCharacter* a_player)
		{
			std::vector<RE::TESObjectCELL*> cells;
			auto* cell = a_player->parentCell;
			if (!cell) {
				return cells;
			}
			cells.push_back(cell);
			if (a_player->GetInterior()) {
				return cells;
			}
			auto* world = a_player->GetWorldSpace();
			if (!world || !world->cellMap) {
				return cells;
			}
			const auto& p = a_player->data.location;
			const int   cx = static_cast<int>(std::floor(p.x / kCellSize)), cy = static_cast<int>(std::floor(p.y / kCellSize));
			std::uint32_t found = 0;
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					// Oblivion's worldspace cell map key: x in the high half, y in the low half
					const std::int32_t key = static_cast<std::int32_t>((static_cast<std::uint32_t>(cx + dx) << 16) | (static_cast<std::uint32_t>(cy + dy) & 0xFFFF));
					const auto it = world->cellMap->find(key);
					if (it == world->cellMap->end() || !it->second) {
						continue;
					}
					++found;
					if (std::ranges::find(cells, it->second) == cells.end()) {
						cells.push_back(it->second);
					}
				}
			}
			static int lastFound = -1;
			if (static_cast<int>(found) != lastFound) {
				lastFound = static_cast<int>(found);
				logger::debug("selection: outside at cell ({}, {}) - {} of the 9 cells around found in the worldspace's cell map", cx, cy, found);
			}
			return cells;
		}

		std::vector<Candidate> Gather(RE::PlayerCharacter* a_player, const aim::View& a_view, const sel::View& a_s)
		{
			std::vector<Candidate> out;
			const auto  cells = NearbyCells(a_player);
			const auto& me = a_player->data.location;
			const float cosMax = std::cos(a_s.maxAngle * std::numbers::pi_v<float> / 180.0f);
			g_cellsScanned = static_cast<std::uint32_t>(cells.size());
			for (auto* cell : cells) {
				if (!cell) {
					continue;
				}
				for (auto* ref : cell->listReferences) {
					const auto kind = UsableKind(ref, a_player);
					if (kind == RE::FormType::None) {
						continue;
					}
					const auto& at = ref->data.location;
					const float dx = at.x - me.x, dy = at.y - me.y, dz = at.z - me.z;
					const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
					if (dist > a_s.range) {
						continue;
					}
					// out of vertical reach: well below the feet (a scene's bench under the ground - the crash of 11:32) or
					// far above the head
					if (dz < -kBelowFeet || dz > kAboveFeet) {
						continue;
					}
					// aim at the middle of a person or creature, at an item where it lies
					const bool  actor = kind == RE::FormType::NPC || kind == RE::FormType::Creature;
					const float tx = at.x - a_view.eye.x, ty = at.y - a_view.eye.y, tz = at.z + (actor ? 60.0f : 4.0f) - a_view.eye.z;
					const float tl = std::sqrt(tx * tx + ty * ty + tz * tz);
					if (tl < 1.0f) {
						continue;
					}
					const float c = (tx * a_view.fwd.x + ty * a_view.fwd.y + tz * a_view.fwd.z) / tl;
					if (c < cosMax) {
						continue;
					}
					const float angle = std::acos(std::clamp(c, -1.0f, 1.0f)) * 180.0f / std::numbers::pi_v<float>;
					out.push_back({ ref, angle, dist, angle / a_s.maxAngle + 0.35f * dist / a_s.range });
				}
			}
			std::ranges::sort(out, {}, &Candidate::score);
			return out;
		}

		RE::TESObjectREFR** Field(RE::InterfaceManager* a_im, int a_which)
		{
			switch (a_which) {
			case sel::kPickRef: return &a_im->pickRef;
			case sel::kReticleRef: return &a_im->reticleRef;
			case sel::kCrosshairRef: return &a_im->crosshairRef;
			case sel::kActivateRef: return &a_im->activateRef;
			default: return nullptr;
			}
		}

		// every field a mode writes (all four for kAllFour)
		std::vector<int> FieldsOf(int a_applyTo)
		{
			if (a_applyTo == sel::kAllFour) {
				return { sel::kPickRef, sel::kReticleRef, sel::kCrosshairRef, sel::kActivateRef };
			}
			if (a_applyTo > sel::kObserve && a_applyTo < sel::kAllFour) {
				return { a_applyTo };
			}
			return {};
		}

		// the game's pick chain, logged on every change (the probe the plan asks for)
		void WatchGameFields(RE::InterfaceManager* a_im, bool a_log)
		{
			struct Seen
			{
				RE::TESObjectREFR* refs[5]{};
				bool               fuzzy = false;
				std::int32_t       pickDistance = 0;
				bool operator==(const Seen&) const = default;
			};
			static Seen last;
			Seen        now;
			now.refs[0] = a_im->pickRef;
			now.refs[1] = a_im->reticleRef;
			now.refs[2] = a_im->crosshairRef;
			now.refs[3] = a_im->activateRef;
			now.refs[4] = a_im->telekinesisRef;
			now.fuzzy = a_im->fuzzyActivatePick;
			now.pickDistance = a_im->pickDistance;
			if (now == last) {
				return;
			}
			last = now;
			static constexpr const char* names[5]{ "pickRef", "reticleRef", "crosshairRef", "activateRef", "telekinesisRef" };
			json fields = json::object();
			std::string line;
			for (int i = 0; i < 5; ++i) {
				const bool ours = now.refs[i] && now.refs[i] == g_written;
				const std::string n = now.refs[i] ? Name(now.refs[i]) + (ours ? " (ours)" : "") : "-";
				fields[names[i]] = n;
				line += std::format("{}{} {}", i ? ", " : "", names[i], n);
			}
			fields["fuzzyActivatePick"] = now.fuzzy;
			fields["pickDistance"] = now.pickDistance;
			fields["activatePickLocation"] = json::array({ a_im->activatePickLocation.x, a_im->activatePickLocation.y, a_im->activatePickLocation.z });
			{
				std::scoped_lock l(g_lock);
				g_gameFields = fields;
			}
			if (a_log) {
				logger::info("game pick: {}; fuzzy {}, pick distance {}", line, now.fuzzy, now.pickDistance);
			}
		}

		void Publish(Status a_status, const std::vector<Candidate>& a_top)
		{
			if (!a_status.active) {
				marker::Show(nullptr);   // off, in a menu, the other view, not calibrated: nothing to mark
			}
			// where the frame stopped, once per change - an info-level log then shows which stage a round reached
			static int lastReason = -1;
			if (static_cast<int>(a_status.reason) != lastReason) {
				lastReason = static_cast<int>(a_status.reason);
				logger::info("state: {}", a_status.reason == Reason::kActive ? "selecting" : a_status.why);
			}
			std::vector<json> top;
			for (std::size_t i = 0; i < a_top.size() && i < 5; ++i) {
				const auto& c = a_top[i];
				auto*       base = c.ref->data.objectReference;
				top.push_back({ { "ref", Name(c.ref) }, { "type", base ? std::string(RE::FormTypeToString(base->GetFormType())) : "?" },
					{ "angle", std::round(c.angle * 10) / 10 }, { "dist", std::round(c.dist) }, { "score", std::round(c.score * 1000) / 1000 },
					{ "altar_loaded", c.ref->isAltarRefLoaded }, { "oblivion_loaded", c.ref->isOblivionRefLoaded } });
			}
			std::scoped_lock l(g_lock);
			g_status = std::move(a_status);
			g_top = std::move(top);
		}
	}

	void Tick()
	{
		const auto s = sel::Snapshot();
		auto*      im = RE::InterfaceManager::GetInstance(false, false);
		auto*      player = RE::PlayerCharacter::GetSingleton();
		Status     st;
		st.applyTo = sel::ApplyToName(s.applyTo);
		if (!im || !player) {
			st.reason = Reason::kNotReady;
			st.why = "the game is not ready";
			Publish(st, {});
			return;
		}

		// a press waiting for its outcome: a menu (container, dialogue, loading) or the item leaving the world means the
		// game acted on it; after kPressWaitMs with neither, the game refused it - logged, and left alone (nothing is ever
		// activated from here: the engine traps reference changes off its TES thread)
		if (g_press.id) {
			if (im->menuMode != 1) {
				logger::info("activate: a menu opened - the game handled the press on [{:08X}]", g_press.id);
				g_press = {};
			} else if (auto* r = StillThere(g_press.id); !r) {
				logger::info("activate: [{:08X}] left the world - the game handled the press", g_press.id);
				g_press = {};
			} else if (GetTickCount64() - g_press.at >= kPressWaitMs) {
				// the game did not act: it refused (out of its reach, not usable from here). Left alone - activating it from
				// here is a change of a reference's state off the TES thread, which the engine traps (crash 11:32:40, a bench)
				logger::info("activate: the game did not act on [{:08X}] - left alone", g_press.id);
				g_press = {};
			}
		}

		// last frame's write: did the game keep it? (logged once per change of the answer)
		if (g_written) {
			int held = 1;
			for (const int f : FieldsOf(g_writtenTo)) {
				if (auto** p = Field(im, f); p && *p != g_written) {
					held = 0;
				}
			}
			(held ? g_writesKept : g_writesReplaced) += 1;
			if (const ULONGLONG now = GetTickCount64(); now - g_writeSummaryAt >= 5000) {
				if (g_writeSummaryAt) {
					logger::debug("write check, last 5 s: {} kept {} times, replaced by the game {} times by the next frame",
						sel::ApplyToName(g_writtenTo), g_writesKept, g_writesReplaced);
				}
				g_writeSummaryAt = now;
				g_writesKept = g_writesReplaced = 0;
			}
		}

		WatchGameFields(im, s.logTargets);

		const auto clearOurs = [&] {
			if (!g_written) {
				return;
			}
			for (const int f : FieldsOf(g_writtenTo)) {
				if (auto** p = Field(im, f); p && *p == g_written) {
					*p = nullptr;   // only what this mod put there; the game's own value is never touched
				}
			}
			g_written = nullptr;
		};

		if (!s.enabled) {
			clearOurs();
			st.reason = Reason::kOff;
			st.why = "switched off";
			Publish(st, {});
			return;
		}
		if (im->menuMode != 1) {   // Oblivion Remastered: 1 is gameplay, anything else a menu (as Tween Menu and Improved Wheel Menu read it)
			clearOurs();   // this mod's own activateRef write is taken back while a menu (or a load) is up
			st.reason = Reason::kMenu;
			st.why = "a menu is open";
			Publish(st, {});
			return;
		}
		const auto view = aim::Read(player);   // read in either view, so the calibration keeps learning
		// the view, debounced: is3rdPerson reads false for a single frame every few seconds in third person (round 2,
		// 09:49 - "off in first person" for ~13 ms), which would drop the write for that frame; a switch counts once it
		// has held for 150 ms
		static bool      third = player->is3rdPerson;
		static ULONGLONG differentSince = 0;
		if (player->is3rdPerson != third) {
			const ULONGLONG now = GetTickCount64();
			if (!differentSince) {
				differentSince = now;
			} else if (now - differentSince >= 150) {
				third = player->is3rdPerson;
				differentSince = 0;
			}
		} else {
			differentSince = 0;
		}
		if (third ? !s.thirdPerson : !s.firstPerson) {
			clearOurs();
			st.reason = third ? Reason::kOffThird : Reason::kOffFirst;
			st.why = third ? "off in third person" : "off in first person";
			Publish(st, {});
			return;
		}
		if (!view.ok) {
			clearOurs();
			st.reason = aim::Calibrated() ? Reason::kNoCamera : Reason::kCalibrating;
			st.why = aim::Calibrated() ? "the camera cannot be read" : "calibrating - walk a few steps";
			Publish(st, {});
			return;
		}

		// the game's own pick: whichever of its fields holds something this mod did not put there
		RE::TESObjectREFR* game = nullptr;
		for (auto* r : { im->activateRef, im->crosshairRef, im->reticleRef, im->pickRef }) {
			if (r && r != g_written) {
				game = r;
				break;
			}
		}

		const auto candidates = Gather(player, view, s);
		RE::TESObjectREFR* choice = candidates.empty() ? nullptr : candidates.front().ref;
		st.active = true;
		st.reason = Reason::kActive;
		st.candidates = static_cast<std::uint32_t>(candidates.size());
		st.choice = choice ? Name(choice) : "";
		st.game = game ? Name(game) : "";

		static RE::TESObjectREFR* lastChoice = nullptr;
		if (choice != lastChoice) {
			lastChoice = choice;
			if (s.logTargets) {
				if (choice) {
					logger::info("selection: {} ({:.1f} degrees, {:.0f} units, {} candidates){}", Name(choice), candidates.front().angle,
						candidates.front().dist, candidates.size(), game ? std::format(" - the game's own pick {} wins", Name(game)) : "");
				} else {
					logger::info("selection: nothing within {:.0f} units and {:.0f} degrees", s.range, s.maxAngle);
				}
			}
		}

		// the Activate press, watched only: whether the game acted on this mod's choice goes into the log
		if (activate::PressedThisTick(aim::PlayerController())) {
			if (game) {
				logger::info("activate: pressed - the game's own pick {} is used", Name(game));
			} else if (!choice) {
				logger::info("activate: pressed - nothing chosen");
			} else if (s.applyTo == sel::kObserve) {
				logger::info("activate: pressed - observing only (iApplyTo=0), {} is not activated", Name(choice));
			} else {
				// watched only: the game's own activation acts on the activateRef write; the log says whether it did
				g_press = { choice->GetFormID(), GetTickCount64() };
				logger::info("activate: pressed on {} - watching for the game to act on it", Name(choice));
			}
		}

		// the choice goes into activateRef only where the game picked nothing (the game's own pick always wins)
		if (s.applyTo != sel::kObserve && choice && !game) {
			if (g_written && g_writtenTo != s.applyTo) {
				clearOurs();
			}
			for (const int f : FieldsOf(s.applyTo)) {
				if (auto** p = Field(im, f)) {
					*p = choice;
				}
			}
			g_written = choice;
			g_writtenTo = s.applyTo;
		} else {
			clearOurs();
		}
		// the marker: what Activate will use when it is this mod's choice (the game's own pick shows the game's prompt)
		marker::Show(s.showMarker && s.applyTo != sel::kObserve && choice && !game ? choice : nullptr);
		Publish(st, candidates);
	}

	Status GetStatus()
	{
		std::scoped_lock l(g_lock);
		return g_status;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "active", g_status.active }, { "why", g_status.why }, { "choice", g_status.choice }, { "game", g_status.game },
			{ "candidates", g_status.candidates }, { "apply_to", g_status.applyTo }, { "cells", g_cellsScanned.load() }, { "top", g_top },
			{ "game_fields", g_gameFields } };
	}
}
