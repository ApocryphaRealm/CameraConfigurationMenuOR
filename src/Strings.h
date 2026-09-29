#pragma once

// Every visible string goes through TR("Key", "English") (rule 66). The eleven files are UTF-16LE with a BOM,
// "$CCM_<Key><TAB>text" per line, and ship in the FRAMEWORK's translation folder -
// OBSE\Plugins\ApocryphaMenuFramework\Translations\CameraConfigurationMenu_<language>.txt - because AMF builds its
// font atlas from every <Mod>_<language>.txt there (AMF-OR Strings.cpp): a Japanese or Chinese CCM page gets its
// glyphs without CCM touching fonts. The language is AMF::Language(), re-read each frame the page draws.

namespace strings
{
	void Refresh();   // cheap; reloads only when AMF's language changed
	const char* Get(const char* a_key, const char* a_english);   // pointer stays valid until the next reload
	const std::string& Language();
	std::size_t Count();
}

#define TR(key, english) ::strings::Get("CCM_" key, english)
