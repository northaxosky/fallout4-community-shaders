# FO4 Community Shaders

Port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) to Fallout 4.

Currently in early bootstrap. The first absorbed feature is **MotionVectorFixes** (lifted from [FO4Upscaling](https://github.com/northaxosky/FO4Upscaling)).

## Features

| Feature | Status | Description |
|---------|--------|-------------|
| MotionVectorFixes | Implemented | Fixes weapon ghosting, menu ghosting, animated objects, LOD motion vectors |

More features coming as the port progresses.

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (Desktop C++ workload)
- [CMake 3.21+](https://cmake.org/)
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` environment variable set
- [Git](https://git-scm.com/)

## User Requirements

- [Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)
- [Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)

## Build

```bash
git clone --recursive https://github.com/<owner>/fallout4-community-shaders.git
cd fallout4-community-shaders

# Fetch proprietary SDK runtime DLLs (NVIDIA Streamline, AMD FidelityFX, Intel XeSS)
./scripts/fetch-sdks.sh

cmake -S . --preset=default
cmake --build build --config Release
```

Output: `build/Release/FO4CommunityShaders.dll` plus runtime SDK DLLs staged under `package/F4SE/Plugins/`.

## Deploy

```bash
cp scripts/.env.example scripts/.env
# Edit scripts/.env with your MO2 mod folder + vcpkg path

./scripts/deploy.sh build    # build + deploy
./scripts/deploy.sh deploy   # deploy only (skip build)
```

## Project structure

```
src/                          Core: Feature framework, F4SE entry
features/<Name>/              One subdirectory per feature
extern/                       Submodules: CommonLibF4, FidelityFX-SDK, Streamline, XeSS
include/                      Shared headers (PCH, Detours static lib)
cmake/                        Build config (Common.cmake, Plugin.h.in, Version.rc.in)
package/F4SE/Plugins/         Runtime files staged here for MO2 deployment
scripts/                      deploy.sh, fetch-sdks.sh (added when SDK-using features absorbed)
```

## License

GPL-3.0-or-later with Modding Exception and GPL-3.0 Linking Exception (matching upstream Skyrim Community Shaders). See `LICENSE` and `EXCEPTIONS.md`.
