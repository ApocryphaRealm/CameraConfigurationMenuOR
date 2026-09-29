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
			CCM_ROW("Keys", "iShoulderSwapKey", kInt, shoulderSwapKey, 37, 0, 255, "Keyboard keys are DirectInput scan codes, 0 = none. 37 = K: move the camera to the other shoulder."),
			CCM_ROW("Keys", "iCycleStyleKey", kInt, cycleStyleKey, 38, 0, 255, "38 = L: switch between the two free camera styles."),
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
		const auto dot = a_name.find('.');
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
