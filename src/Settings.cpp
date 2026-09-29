#include "Settings.h"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace settings
{
	namespace
	{
		using K = Field::Kind;
#define CCM_ROW(sec, key, kind, member, def, lo, hi, comment) Field{ sec, key, K::kind, offsetof(Values, member), def, lo, hi, comment }
		// an indexed member must land inside its array (std::array's operator[] once sent every framing row to offset 0-12)
		static_assert(offsetof(Values, fmGroups[7].distance) ==
		              offsetof(Values, fmGroups) + 7 * sizeof(Values::FramingGroup) + offsetof(Values::FramingGroup, distance));

		// ONE ROW PER LINE - tools/write-ini.py parses these rows to write the shipped INI.
		const std::vector<Field> kTable = {
			CCM_ROW("General", "bEnabled", kBool, enabled, 1, 0, 1, "1 = CCM runs. 0 puts back the game's own camera values that CCM recorded."),
			CCM_ROW("General", "iCameraStyle", kInt, cameraStyle, 1, 0, 2, "0 = the game's camera, 1 = free camera (turn to face on block, cast and attack), 2 = free camera only with the weapon sheathed."),
			CCM_ROW("General", "fBlockTurnSeconds", kFloat, blockTurnSeconds, 0.20, 0, 3, "Seconds the body keeps facing the camera after a block starts (free camera style)."),
			CCM_ROW("General", "fSpellTurnSeconds", kFloat, spellTurnSeconds, 1.40, 0, 3, "Seconds the body keeps facing the camera after a spell cast starts."),
			CCM_ROW("General", "fAttackTurnSeconds", kFloat, attackTurnSeconds, 0.60, 0, 3, "Seconds the body keeps facing the camera after an attack or bow shot starts."),
			CCM_ROW("General", "bFaceWhileHeld", kBool, faceWhileHeld, 0, 0, 1, "1 = keep facing the camera for as long as block or the attack button is held."),
			CCM_ROW("General", "bFaceWhileLockedOn", kBool, faceWhileLockedOn, 1, 0, 1, "1 = while Ultimate Combat is locked on to a target, no free camera: the body faces where the camera looks (the dodge directions need it)."),
			CCM_ROW("General", "fBodyTurnSpeed", kFloat, bodyTurnSpeed, 0, 0, 1440, "Degrees per second the body turns while facing the camera. 0 = the game's own speed."),
			CCM_ROW("General", "bCompassFollowsCamera", kBool, compassFollowsCamera, 1, 0, 1, "1 = the compass shows where the camera looks, not where the body faces (free camera only)."),
			CCM_ROW("General", "bFreeCameraOnHorse", kBool, freeCameraOnHorse, 0, 0, 1, "1 = the free camera also on horseback."),
			CCM_ROW("General", "bStandDownInDialogue", kBool, standDownInDialogue, 1, 0, 1, "1 = the game's own camera during dialogue."),
			CCM_ROW("General", "bStandDownSitting", kBool, standDownSitting, 1, 0, 1, "1 = the game's own camera while sitting or using furniture."),
			CCM_ROW("General", "bVanityCamera", kBool, vanityCamera, 1, 0, 1, "1 = the game's idle camera that circles the player is left on."),
			CCM_ROW("Keys", "iShoulderSwapKey", kInt, shoulderSwapKey, 26, 0, 255, "Keyboard keys are DirectInput scan codes, 0 = none. 26 = [: move the camera to the other shoulder."),
			CCM_ROW("Keys", "iCycleStyleKey", kInt, cycleStyleKey, 27, 0, 255, "27 = ]: switch between the two free camera styles."),
			CCM_ROW("Keys", "iToggleKey", kInt, toggleKey, 0, 0, 255, "Turn CCM on and off."),
			CCM_ROW("Keys", "iNextPresetKey", kInt, nextPresetKey, 0, 0, 255, "Load the next saved preset."),
			CCM_ROW("Keys", "iHeightOffsetKey", kInt, heightOffsetKey, 0, 0, 255, "Raise or lower the camera by the height offset."),
			CCM_ROW("Keys", "iCustomGroupKey", kInt, customGroupKey, 0, 0, 255, "Switch to the Custom framing group."),
			CCM_ROW("Keys", "iShoulderSwapButton", kInt, shoulderSwapButton, 0, 0, 65535, "Controller buttons are XInput masks, 0 = none. Every button already has a game action, so none ship bound."),
			CCM_ROW("Keys", "iCycleStyleButton", kInt, cycleStyleButton, 0, 0, 65535, ""),
			CCM_ROW("Keys", "iToggleButton", kInt, toggleButton, 0, 0, 65535, ""),
			CCM_ROW("Keys", "iNextPresetButton", kInt, nextPresetButton, 0, 0, 65535, ""),
			CCM_ROW("Keys", "iHeightOffsetButton", kInt, heightOffsetButton, 0, 0, 65535, ""),
			CCM_ROW("Keys", "iCustomGroupButton", kInt, customGroupButton, 0, 0, 65535, ""),
			CCM_ROW("Framing.Standing", "fSide", kFloat, fmGroups[0].side, 0, -150, 150, "Centimetres added to the game's sideways camera offset: + moves the camera further right of your character (it mirrors with the shoulder swap). 0 = the game's. Standing is also used by every context below whose bOwn is 0."),
			CCM_ROW("Framing.Standing", "fHeight", kFloat, fmGroups[0].height, 0, -100, 150, "Centimetres added to the camera's height."),
			CCM_ROW("Framing.Standing", "fDistance", kFloat, fmGroups[0].distance, 0, -300, 600, "Centimetres added to the camera's distance behind your character."),
			CCM_ROW("Framing.Moving", "bOwn", kBool, fmGroups[1].own, 0, 0, 1, "Walking or running. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Moving", "fSide", kFloat, fmGroups[1].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Moving", "fHeight", kFloat, fmGroups[1].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Moving", "fDistance", kFloat, fmGroups[1].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.Sprinting", "bOwn", kBool, fmGroups[2].own, 0, 0, 1, "Sprinting. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Sprinting", "fSide", kFloat, fmGroups[2].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Sprinting", "fHeight", kFloat, fmGroups[2].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Sprinting", "fDistance", kFloat, fmGroups[2].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.Sneaking", "bOwn", kBool, fmGroups[3].own, 0, 0, 1, "Sneaking (above sprinting, a drawn weapon and moving). 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Sneaking", "fSide", kFloat, fmGroups[3].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Sneaking", "fHeight", kFloat, fmGroups[3].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Sneaking", "fDistance", kFloat, fmGroups[3].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.WeaponDrawn", "bOwn", kBool, fmGroups[4].own, 0, 0, 1, "A weapon other than a bow, or fists, out - when not sneaking or sprinting. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.WeaponDrawn", "fSide", kFloat, fmGroups[4].side, 0, -150, 150, ""),
			CCM_ROW("Framing.WeaponDrawn", "fHeight", kFloat, fmGroups[4].height, 0, -100, 150, ""),
			CCM_ROW("Framing.WeaponDrawn", "fDistance", kFloat, fmGroups[4].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.Bow", "bOwn", kBool, fmGroups[5].own, 0, 0, 1, "A bow out (drawn or not) - above sneaking, sprinting and moving. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Bow", "fSide", kFloat, fmGroups[5].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Bow", "fHeight", kFloat, fmGroups[5].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Bow", "fDistance", kFloat, fmGroups[5].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.Swimming", "bOwn", kBool, fmGroups[6].own, 0, 0, 1, "Swimming. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Swimming", "fSide", kFloat, fmGroups[6].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Swimming", "fHeight", kFloat, fmGroups[6].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Swimming", "fDistance", kFloat, fmGroups[6].distance, 0, -300, 600, ""),
			CCM_ROW("Framing.Horseback", "bOwn", kBool, fmGroups[7].own, 0, 0, 1, "On horseback. 1 = its own position below; 0 = Standing's."),
			CCM_ROW("Framing.Horseback", "fSide", kFloat, fmGroups[7].side, 0, -150, 150, ""),
			CCM_ROW("Framing.Horseback", "fHeight", kFloat, fmGroups[7].height, 0, -100, 150, ""),
			CCM_ROW("Framing.Horseback", "fDistance", kFloat, fmGroups[7].distance, 0, -300, 600, ""),
			CCM_ROW("Smoothing", "bEaseOffsets", kBool, smEaseOffsets, 1, 0, 1, "1 = a new camera position (a setting, a change of context, the shoulder swap) slides in instead of cutting."),
			CCM_ROW("Smoothing", "iOffsetEasing", kInt, smOffsetEasing, 15, 0, 21, "The slide's curve: 0 linear; 1-3 quadratic, 4-6 cubic, 7-9 quartic, 10-12 quintic, 13-15 sine, 16-18 circular, 19-21 exponential (each in, out, in-out)."),
			CCM_ROW("Smoothing", "fOffsetSeconds", kFloat, smOffsetSeconds, 0.5, 0, 5, "Seconds the slide takes."),
			CCM_ROW("Smoothing", "fFollowSpeed", kFloat, smFollowSpeed, -1, -1, 30, "How tightly the camera follows your character's movement: higher is tighter, 0 = no smoothing. -1 = the game's."),
			CCM_ROW("Smoothing", "fMaxLagDistance", kFloat, smMaxLagDistance, -1, -1, 500, "The furthest the camera may trail behind, in centimetres. 0 = no limit, -1 = the game's."),
			CCM_ROW("Smoothing", "fRotationSpeedPitch", kFloat, smRotationPitch, -1, -1, 30, "How tightly the camera follows looking up and down: higher is tighter, 0 = no smoothing. -1 = the game's."),
			CCM_ROW("Smoothing", "fRotationSpeedYaw", kFloat, smRotationYaw, -1, -1, 30, "How tightly the camera follows turning left and right. -1 = the game's."),
			CCM_ROW("Smoothing", "bSprintShake", kBool, smSprintShake, 1, 0, 1, "Screen shake while sprinting: 1 = the game's shake, 0 = a steady camera."),
			CCM_ROW("Smoothing", "fStateBlendSeconds", kFloat, smStateBlendSeconds, -1, -1, 5, "Seconds the game takes to blend between its camera states (walking, sprinting, weapon drawn...). -1 = the game's."),
			CCM_ROW("Selection", "bEnabled", kBool, selEnabled, 1, 0, 1, "1 = use what you are roughly looking at: anything within reach and angle, closest to the aim first. The crosshair's own target always wins."),
			CCM_ROW("Selection", "bThirdPerson", kBool, selThirdPerson, 1, 0, 1, "1 = in third person."),
			CCM_ROW("Selection", "bFirstPerson", kBool, selFirstPerson, 0, 0, 1, "1 = also in first person (the game's precise aim otherwise)."),
			CCM_ROW("Selection", "bShowMarker", kBool, selShowMarker, 1, 0, 1, "1 = the name of what Activate will use is drawn where it stands."),
			CCM_ROW("Selection", "fRange", kFloat, selRange, 300, 50, 400, "How far from your character, in game units (about 70 to a metre)."),
			CCM_ROW("Selection", "fMaxAngle", kFloat, selMaxAngle, 35, 5, 75, "How far to either side of where the camera looks, in degrees."),
			CCM_ROW("Selection", "bLogTargets", kBool, selLogTargets, 0, 0, 1, "1 = a log line whenever the game's pick or this choice changes (for testing; fills the log)."),
			CCM_ROW("Crosshair", "iMode", kInt, xhMode, 0, 0, 2, "0 = the game's crosshair, 1 = contextual (shown only when it means something, below), 2 = always hidden."),
			CCM_ROW("Crosshair", "bWhenAiming", kBool, xhWhenAiming, 1, 0, 1, "Contextual: shown while a bow is drawn or a spell is being cast."),
			CCM_ROW("Crosshair", "bWhenTarget", kBool, xhWhenTarget, 1, 0, 1, "Contextual: shown while there is something to activate."),
			CCM_ROW("Crosshair", "bWhenWeaponDrawn", kBool, xhWhenWeaponDrawn, 0, 0, 1, "Contextual: shown while a weapon is drawn."),
			CCM_ROW("Crosshair", "bInFirstPerson", kBool, xhInFirstPerson, 1, 0, 1, "Contextual: always shown in first person."),
			CCM_ROW("Crosshair", "fFadeSeconds", kFloat, xhFadeSeconds, 0.15, 0, 2, "Seconds the crosshair takes to fade in or out."),
			CCM_ROW("Presets", "iActive", kInt, activePreset, 0, 0, 6, "The preset slot loaded last (1-6), 0 = none."),
			CCM_ROW("Log", "uLogLevel", kInt, logLevel, 2, 0, 6, "0 trace, 1 debug, 2 info, 3 warnings, 4 errors, 5 critical, 6 off. Use 1 when reporting a problem."),
		};
#undef CCM_ROW

		Values     g_values;
		std::mutex g_saveLock;
		bool       g_tableBroken = false;   // two rows share memory: never load or save (the INI is left as it is)

		std::size_t SizeOf(const Field& a_f) { return a_f.kind == K::kBool ? sizeof(bool) : 4; }

		// Every row must own its bytes inside Values, and no two rows may share any: a row that points at the wrong
		// member reads and writes that member (2026-09-29 - the framing rows landed on enabled / cameraStyle).
		bool TableSound()
		{
			static const bool sound = [] {
				bool ok = true;
				for (std::size_t i = 0; i < kTable.size(); ++i) {
					const auto& a = kTable[i];
					if (a.offset + SizeOf(a) > sizeof(Values)) {
						logger::critical("settings: [{}] {} points outside the settings block - a build defect", a.section, a.key);
						ok = false;
					}
					for (std::size_t j = i + 1; j < kTable.size(); ++j) {
						const auto& b = kTable[j];
						if (a.offset < b.offset + SizeOf(b) && b.offset < a.offset + SizeOf(a)) {
							logger::critical("settings: [{}] {} and [{}] {} share memory (offset {} / {}) - a build defect", a.section, a.key, b.section, b.key, a.offset, b.offset);
							ok = false;
						}
					}
				}
				if (!ok) logger::critical("settings: the table is unsound - CCM runs on its compiled defaults and will not read or write the INI");
				return ok;
			}();
			return sound;
		}

		std::string_view Trim(std::string_view a_s)
		{
			while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t')) a_s.remove_prefix(1);
			while (!a_s.empty() && (a_s.back() == ' ' || a_s.back() == '\t' || a_s.back() == '\r')) a_s.remove_suffix(1);
			return a_s;
		}

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::uint8_t* Slot(Values& a_v, const Field& a_f) { return reinterpret_cast<std::uint8_t*>(&a_v) + a_f.offset; }
		const std::uint8_t* Slot(const Values& a_v, const Field& a_f) { return reinterpret_cast<const std::uint8_t*>(&a_v) + a_f.offset; }

		double Read(const Values& a_v, const Field& a_f)
		{
			switch (a_f.kind) {
			case K::kBool:  return *reinterpret_cast<const bool*>(Slot(a_v, a_f)) ? 1.0 : 0.0;
			case K::kInt:   return *reinterpret_cast<const std::int32_t*>(Slot(a_v, a_f));
			default:        return *reinterpret_cast<const float*>(Slot(a_v, a_f));
			}
		}

		void Write(Values& a_v, const Field& a_f, double a_x)
		{
			a_x = std::clamp(a_x, a_f.min, a_f.max);
			switch (a_f.kind) {
			case K::kBool:  *reinterpret_cast<bool*>(Slot(a_v, a_f)) = a_x != 0.0; break;
			case K::kInt:   *reinterpret_cast<std::int32_t*>(Slot(a_v, a_f)) = static_cast<std::int32_t>(a_x); break;
			default:        *reinterpret_cast<float*>(Slot(a_v, a_f)) = static_cast<float>(a_x); break;
			}
		}

		std::string Format(const Field& a_f, double a_x)
		{
			switch (a_f.kind) {
			case K::kBool:  return a_x != 0.0 ? "1" : "0";
			case K::kInt:   return std::to_string(static_cast<std::int32_t>(a_x));
			default:        return std::format("{:.2f}", a_x);
			}
		}

		std::optional<double> Parse(const Field& a_f, std::string_view a_text)
		{
			a_text = Trim(a_text);
			if (a_text.empty()) {
				return std::nullopt;
			}
			if (a_f.kind == K::kFloat) {
				double x = 0.0;
				const auto r = std::from_chars(a_text.data(), a_text.data() + a_text.size(), x);
				return r.ec == std::errc{} ? std::optional<double>(x) : std::nullopt;
			}
			std::int64_t x = 0;
			const bool hex = a_text.size() > 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X');
			const auto* b = a_text.data() + (hex ? 2 : 0);
			const auto r = std::from_chars(b, a_text.data() + a_text.size(), x, hex ? 16 : 10);
			return r.ec == std::errc{} ? std::optional<double>(static_cast<double>(x)) : std::nullopt;
		}

		std::filesystem::path ModuleFolder()
		{
			HMODULE self = nullptr;
			::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ModuleFolder), &self);
			wchar_t buf[MAX_PATH]{};
			const DWORD n = self ? ::GetModuleFileNameW(self, buf, MAX_PATH) : 0;
			if (n == 0 || n >= MAX_PATH) {
				return std::filesystem::path(L"OBSE") / L"Plugins";   // relative to the game's working folder
			}
			return std::filesystem::path(buf).parent_path();
		}
	}

	const std::vector<Field>& Table() { return kTable; }
	Values& Get() { return g_values; }
	Values Defaults() { return Values{}; }
	std::filesystem::path PluginFolder() { return ModuleFolder(); }
	std::filesystem::path IniPath() { return ModuleFolder() / L"CameraConfigurationMenu.ini"; }

	void Load()
	{
		if (!TableSound()) {
			g_tableBroken = true;
			return;
		}
		// Rule 16, checked where it can be: the member initialisers and the table must agree.
		const Values d{};
		for (const auto& f : kTable) {
			if (std::abs(Read(d, f) - f.def) > 1e-6) {
				logger::error("settings: compiled default of [{}] {} is {} but the table says {} - a build defect, report it",
					f.section, f.key, Read(d, f), f.def);
			}
		}

		std::ifstream file(IniPath());
		if (!file.is_open()) {
			logger::info("settings: {} not found - the compiled defaults are in effect (they match the shipped INI)", IniPath().string());
			return;
		}
		std::unordered_map<std::string, std::string> entries;
		std::string line, section;
		while (std::getline(file, line)) {
			const auto t = Trim(line);
			if (t.empty() || t.front() == ';' || t.front() == '#') continue;
			if (t.front() == '[' && t.back() == ']') {
				section = Lower(std::string(Trim(t.substr(1, t.size() - 2))));
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) continue;
			entries[section + "." + Lower(std::string(Trim(t.substr(0, eq))))] = std::string(Trim(t.substr(eq + 1)));
		}
		for (const auto& f : kTable) {
			const auto it = entries.find(Lower(std::string(f.section)) + "." + Lower(std::string(f.key)));
			if (it == entries.end()) {
				logger::debug("settings: [{}] {} not in the INI - compiled default {}", f.section, f.key, Format(f, f.def));
				continue;
			}
			const auto x = Parse(f, it->second);
			if (!x) {
				logger::warn("settings: [{}] {} = \"{}\" is not a number - keeping {}", f.section, f.key, it->second, Format(f, Read(g_values, f)));
				continue;
			}
			if (*x < f.min || *x > f.max) {
				logger::warn("settings: [{}] {} = {} is outside {}..{} - clamped", f.section, f.key, *x, f.min, f.max);
			}
			Write(g_values, f, *x);
		}
		logger::info("settings loaded from {}: enabled={}, style={}, keys K={:#x} L={:#x}, log level {}", IniPath().string(),
			g_values.enabled, g_values.cameraStyle, g_values.shoulderSwapKey, g_values.cycleStyleKey, g_values.logLevel);
	}

	bool Save()
	{
		if (g_tableBroken || !TableSound()) return false;   // never write through a table that points at the wrong members
		std::scoped_lock l(g_saveLock);
		const auto path = IniPath();
		std::vector<std::string> lines;
		bool crlf = true;
		{
			std::ifstream in(path, std::ios::binary);
			if (in.is_open()) {
				std::stringstream ss;
				ss << in.rdbuf();
				const std::string all = ss.str();
				crlf = all.find("\r\n") != std::string::npos || all.empty();
				std::string cur;
				for (char c : all) {
					if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); lines.push_back(cur); cur.clear(); }
					else cur.push_back(c);
				}
				if (!cur.empty()) lines.push_back(cur);
			}
		}
		if (lines.empty()) {
			std::istringstream def(DefaultIniText());
			for (std::string s; std::getline(def, s);) { if (!s.empty() && s.back() == '\r') s.pop_back(); lines.push_back(s); }
		}

		std::vector<bool> written(kTable.size(), false);
		std::string section;
		for (auto& ln : lines) {
			const auto t = Trim(ln);
			if (!t.empty() && t.front() == '[' && t.back() == ']') { section = Lower(std::string(Trim(t.substr(1, t.size() - 2)))); continue; }
			if (t.empty() || t.front() == ';' || t.front() == '#') continue;
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) continue;
			const std::string key = Lower(std::string(Trim(t.substr(0, eq))));
			for (std::size_t i = 0; i < kTable.size(); ++i) {
				if (Lower(kTable[i].section) == section && Lower(kTable[i].key) == key) {
					ln = std::string(kTable[i].key) + "=" + Format(kTable[i], Read(g_values, kTable[i]));
					written[i] = true;
					break;
				}
			}
		}
		// keys the file did not have yet go at the end of their section (a new section at the end if needed)
		for (std::size_t i = 0; i < kTable.size(); ++i) {
			if (written[i]) continue;
			const std::string want = Lower(kTable[i].section);
			std::size_t insertAt = lines.size();
			bool found = false;
			std::string sec;
			for (std::size_t j = 0; j < lines.size(); ++j) {
				const auto t = Trim(lines[j]);
				if (!t.empty() && t.front() == '[' && t.back() == ']') {
					if (found) { insertAt = j; break; }
					sec = Lower(std::string(Trim(t.substr(1, t.size() - 2))));
					found = sec == want;
				}
			}
			if (!found) {
				lines.push_back("");
				lines.push_back(std::string("[") + kTable[i].section + "]");
				insertAt = lines.size();
			} else {
				// before the blank lines that separate this section from the next one
				while (insertAt > 0 && Trim(lines[insertAt - 1]).empty()) --insertAt;
			}
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string(kTable[i].key) + "=" + Format(kTable[i], Read(g_values, kTable[i])));
		}

		const auto tmp = std::filesystem::path(path).concat(L".tmp");
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				logger::error("settings: could not write {} - this change lasts only until the game closes", path.string());
				return false;
			}
			for (const auto& ln : lines) { out << ln << (crlf ? "\r\n" : "\n"); }
		}
		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec) {
			logger::error("settings: could not replace {} ({}) - this change lasts only until the game closes", path.string(), ec.message());
			return false;
		}
		logger::debug("settings: saved {}", path.string());
		return true;
	}

	bool SetByName(const std::string& a_name, const json& a_value, std::string& a_why)
	{
		if (g_tableBroken || !TableSound()) {
			a_why = "the settings table is unsound (see the log) - nothing is written";
			return false;
		}
		const auto dot = a_name.rfind('.');   // sections may hold a dot (Framing.Sneaking), keys never do
		const std::string sec = Lower(dot == std::string::npos ? "" : a_name.substr(0, dot));
		const std::string key = Lower(dot == std::string::npos ? a_name : a_name.substr(dot + 1));
		for (const auto& f : kTable) {
			if ((sec.empty() || Lower(f.section) == sec) && Lower(f.key) == key) {
				double x = 0.0;
				if (a_value.is_boolean()) x = a_value.get<bool>() ? 1.0 : 0.0;
				else if (a_value.is_number()) x = a_value.get<double>();
				else { a_why = "value must be a number or a boolean"; return false; }
				if (x < f.min || x > f.max) { a_why = std::format("{} is outside {}..{}", x, f.min, f.max); return false; }
				Write(g_values, f, x);
				logger::info("settings: [{}] {} = {} (set by the driving tool)", f.section, f.key, Format(f, x));
				return Save() || (a_why = "set, but the INI could not be written", false);
			}
		}
		a_why = "no setting named " + a_name;
		return false;
	}

	json GetAll()
	{
		json j = json::object();
		for (const auto& f : kTable) {
			const double x = Read(g_values, f);
			j[std::string(f.section) + "." + f.key] = f.kind == K::kBool ? json(x != 0.0) : f.kind == K::kInt ? json(static_cast<std::int32_t>(x)) : json(x);
		}
		return j;
	}

	std::string DefaultIniText()
	{
		std::string s = "; CCM - Camera Configuration Menu (Oblivion Remastered). Every setting is also on the CCM page of the\n"
		                "; Apocrypha Menu Framework, which rewrites this file; edits made while the game is closed are read at start.\n";
		std::string section;
		for (const auto& f : kTable) {
			if (section != f.section) {
				section = f.section;
				s += "\n[" + section + "]\n";
			}
			if (f.comment && *f.comment) s += std::string("; ") + f.comment + "\n";
			s += std::string(f.key) + "=" + Format(f, f.def) + "\n";
		}
		return s;
	}
}
