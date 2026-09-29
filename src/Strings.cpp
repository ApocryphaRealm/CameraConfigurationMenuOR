#include "Strings.h"

#include "AMF.h"
#include "Settings.h"

#include <fstream>

namespace strings
{
	namespace
	{
		std::mutex                                   g_lock;
		std::unordered_map<std::string, std::string> g_texts;
		std::string                                  g_language;   // empty = never loaded

		std::filesystem::path Dir()
		{
			return settings::PluginFolder() / L"ApocryphaMenuFramework" / L"Translations";
		}

		std::string ToUtf8(std::wstring_view a_w)
		{
			if (a_w.empty()) return {};
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
			if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// -1 no file, -2 not UTF-16LE with a BOM, else how many keys were read
		int ReadInto(const std::filesystem::path& a_path, std::unordered_map<std::string, std::string>& a_out, bool a_overwrite)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) return -1;
			const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) return -2;
			const std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
			int added = 0;
			for (std::size_t pos = 0; pos < text.size();) {
				auto eol = text.find(L'\n', pos);
				if (eol == std::wstring::npos) eol = text.size();
				std::wstring line = text.substr(pos, eol - pos);
				pos = eol + 1;
				if (!line.empty() && line.back() == L'\r') line.pop_back();
				if (line.empty() || line[0] != L'$') continue;
				const auto tab = line.find(L'\t');
				if (tab == std::wstring::npos) continue;
				std::wstring value = line.substr(tab + 1);
				for (std::size_t i = 0; i + 1 < value.size(); ++i) {
					if (value[i] == L'\\' && value[i + 1] == L'n') value.replace(i, 2, L"\n");
				}
				std::string key = ToUtf8(std::wstring_view(line).substr(1, tab - 1));
				if (!a_overwrite && a_out.contains(key)) continue;
				a_out[std::move(key)] = ToUtf8(value);
				++added;
			}
			return added;
		}

		void LoadLocked(const std::string& a_language)
		{
			std::unordered_map<std::string, std::string> texts;
			const auto file = [](const std::string& l) { return Dir() / (L"CameraConfigurationMenu_" + std::wstring(l.begin(), l.end()) + L".txt"); };
			const int own = ReadInto(file(a_language), texts, true);
			const int eng = a_language == "english" ? 0 : ReadInto(file("english"), texts, false);
			if (own < 0) {
				logger::warn("strings: no {} file ({}) - {}", a_language, own == -1 ? "missing" : "not UTF-16LE with a BOM",
					eng > 0 ? "English is used" : "the built-in English is used");
			}
			g_texts = std::move(texts);
			g_language = a_language;
			logger::info("strings: {} loaded, {} texts ({} from the {} file)", a_language, g_texts.size(), own > 0 ? own : 0, a_language);
		}
	}

	void Refresh()
	{
		const std::string lang = AMF::Language();
		std::scoped_lock l(g_lock);
		if (lang != g_language) {
			LoadLocked(lang);
		}
	}

	const char* Get(const char* a_key, const char* a_english)
	{
		std::scoped_lock l(g_lock);
		const auto it = g_texts.find(a_key);
		return it != g_texts.end() && !it->second.empty() ? it->second.c_str() : a_english;
	}

	const std::string& Language() { return g_language; }

	std::size_t Count()
	{
		std::scoped_lock l(g_lock);
		return g_texts.size();
	}
}
