# OBS NoPreventSleep

OBS normally keeps Windows awake while an output such as the replay buffer is active. This Windows x64 plugin disables that sleep-inhibition call so an ordinary system sleep request can proceed.

When the replay buffer is active, the plugin asks OBS to stop it on suspend and restarts it after resume. A replay buffer that was inactive before suspend remains inactive.

## Install

Download `OBS-NoPreventSleep-windows-x64.zip` from the repository's Releases page and extract it into the OBS Studio installation directory. The archive contains:

```text
obs-plugins/64bit/obs-nopreventsleep.dll
```

Restart OBS Studio after installing or updating the plugin. This plugin currently supports Windows x64 only.

## Build locally

Requirements:

- Visual Studio 2022 with the Desktop development with C++ workload
- [Premake 5](https://premake.github.io/)
- An OBS Studio source checkout

Generate and build the Visual Studio solution from PowerShell:

```powershell
$env:OBS_SOURCE_DIR = "C:\path\to\obs-studio"
premake5 vs2022
msbuild build\OBS-Plugin.sln /m /p:Configuration=Release /p:Platform=x64
```

The DLL is written to `bin/Release/obs-nopreventsleep.dll`.

The automated build uses the public headers from OBS Studio 27.2.4 so the module remains loadable on OBS 27 and newer. The plugin resolves the OBS runtime functions dynamically and fails cleanly if a required function is unavailable.

## Automated builds and releases

GitHub Actions builds and integration-tests the x64 DLL for pull requests, pushes to `main`, and manual runs. Every successful run provides a downloadable ZIP artifact.

Pushing a semantic version tag such as `v0.4.0` also creates a GitHub Release and attaches the same ZIP:

```powershell
git tag v0.4.0
git push fork v0.4.0
```
