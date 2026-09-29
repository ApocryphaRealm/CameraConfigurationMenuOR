#pragma once

// CCM's pages in the Apocrypha Menu Framework. Registered at kPostLoad when AMF is installed; without AMF, CCM runs
// from its INI. Page callbacks read and write settings::Values and read game::Status() - never a UObject.

namespace page
{
	inline constexpr const char* kModName = "Camera Configuration Menu";
	void Register();
}
