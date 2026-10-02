-- CCM - Camera Configuration Menu, for The Elder Scrolls IV: Oblivion Remastered (OBSE64 plugin).
-- A free third-person camera (the behaviour of igroshev1990's Player Camera) with per-state framing and smoothing,
-- configured on its own page in the Apocrypha Menu Framework (the owner, 2026-09-28). Plan:
-- D:\Claude output\4. plans\Camera Configuration Menu (CCM)\PLAN.md
-- rule 45: no build-machine paths in any compiled object - set BEFORE includes() so CommonLibOB64's own library
-- target gets it too (a std::source_location in an OBSE header reached through the PCH's absolute -FI path).
-- /d1trimfile strips the project folder from __FILE__ and std::source_location. The flag is wrapped in a TABLE so
-- xmake passes it as one quoted argument: a bare string is split on the path's spaces, and /d1trimfile:"<dir>"
-- reaches cl with the quote characters in the prefix, which then matches nothing (measured 2026-09-29). No trailing
-- separator. /PDBALTPATH:%_PDB% makes the debug directory record only the PDB's file name (it ships beside the DLL).
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("CameraConfigurationMenu")
set_version("1.0.2")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("nlohmann_json")

-- Dear ImGui 1.90.8 docking, the framework's own build (ApocryphaMenuFrameworkOR/xmake.lua): AMF::UseFrameworkImGui()
-- checks the version and struct sizes byte for byte, so this must stay the same source and the same defines. Only the
-- core is compiled - CCM draws inside the framework's frame and has no renderer backend of its own.
target("imgui")
    set_kind("static")
    set_warnings("none")
    add_files("extern/imgui/imgui.cpp", "extern/imgui/imgui_draw.cpp", "extern/imgui/imgui_tables.cpp",
              "extern/imgui/imgui_widgets.cpp")
    add_includedirs("extern/imgui", {public = true})

target("CameraConfigurationMenu")
    add_rules("commonlibob64.plugin", {
        name = "CameraConfigurationMenu",
        author = "ApocryphaRealm",
        description = "CCM - Camera Configuration Menu: a free third-person camera with per-state framing (Oblivion Remastered)"
    })
    add_deps("imgui")
    add_packages("nlohmann_json")
    add_syslinks("user32", "shell32", "ole32")
    on_load(function (target)
        target:add("defines", "CCM_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
    end)
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("include", "src")
    set_pcxxheader("src/pch.h")
