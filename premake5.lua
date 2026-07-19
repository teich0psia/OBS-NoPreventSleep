local obs_source_dir = os.getenv("OBS_SOURCE_DIR") or "../obs-studio"

workspace "OBS-Plugin"
    location "build"
    configurations { "Release" }
    platforms { "x64" }
    architecture "x86_64"

project "OBS-NoPreventSleep"
    kind "SharedLib"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "build/obj/%{cfg.platform}/%{cfg.buildcfg}"
    targetname "obs-nopreventsleep"
    files { "plugin.cpp" }
    includedirs { "compat", path.join(obs_source_dir, "libobs") }
    defines { "WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE" }
    optimize "Speed"
    symbols "On"
    warnings "Extra"
    characterset "Unicode"

    filter "system:windows"
        systemversion "latest"

group "Tests"

project "obs-stub"
    kind "SharedLib"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/tests"
    objdir "build/obj/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}"
    targetname "obs"
    files { "tests/obs-stub.cpp" }
    defines { "WIN32_LEAN_AND_MEAN", "NOMINMAX" }
    optimize "Speed"
    warnings "Extra"

project "obs-frontend-api-stub"
    kind "SharedLib"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/tests"
    objdir "build/obj/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}"
    targetname "obs-frontend-api"
    files { "tests/obs-frontend-api-stub.cpp" }
    defines { "WIN32_LEAN_AND_MEAN", "NOMINMAX" }
    optimize "Speed"
    warnings "Extra"

project "integration-check"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/tests"
    objdir "build/obj/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}"
    files { "tests/integration-check.cpp" }
    defines { "WIN32_LEAN_AND_MEAN", "NOMINMAX", "UNICODE", "_UNICODE" }
    optimize "Speed"
    warnings "Extra"
    characterset "Unicode"

    filter "system:windows"
        systemversion "latest"
