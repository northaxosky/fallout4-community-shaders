# Upstream-author permissions

This repository is a port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) to Fallout 4. The codebase reuses the architecture, feature design, and where applicable the HLSL of the upstream project.

## Permission grant

- **Upstream author:** doodlum (upstream maintainer of Skyrim Community Shaders and the original `doodlum/fo4test` lineage that demonstrated parts of the FO4 hook plumbing reused here).
- **Granted to:** northaxosky, for the purpose of porting and re-releasing Skyrim Community Shaders concepts and code to Fallout 4 as a derived open-source project.
- **Scope:** consent to derive, port, and distribute the Fallout 4 port under the same GPL-3.0-or-later license, including the GPL-3.0 Modding Exception and the GPL-3.0 Linking Exception, matching the upstream project's licensing intent.

The grant covers the act of porting and distributing the derived work. It does not waive any GPL obligations; this repository remains GPL-3.0-or-later with the same modding and linking exceptions as upstream.

## License

This repository is licensed under GPL-3.0-or-later with the Modding Exception and the GPL-3.0 Linking Exception. See [LICENSE](LICENSE) and [EXCEPTIONS.md](EXCEPTIONS.md) for the legal text. The license summary mirrors the upstream Skyrim Community Shaders licensing model.

## Third-party components

Proprietary SDK runtimes that this project bundles (NVIDIA Streamline, AMD FidelityFX-SDK, Intel XeSS) ship under their own vendor licenses. They are fetched into `package/F4SE/Plugins/` by `scripts/fetch-sdks.sh` and are not relicensed by this repository.
