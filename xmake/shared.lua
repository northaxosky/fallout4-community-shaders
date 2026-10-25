function main(target)
    local shared = {
        root = "extern/community-shaders-shared",
        shaders = {
            "package/Shaders/Common/DummyVSTexCoord.hlsl",
            "package/Shaders/Common/FrameBuffer.hlsli",
            "package/Shaders/Common/Math.hlsli",
            "package/Shaders/Common/Game.hlsli",
            "package/Shaders/Common/Color.hlsli",
            "package/Shaders/Common/ColorManagement.hlsli",
            "package/Shaders/Common/ColorSpaces.hlsli",
            "package/Shaders/Common/TransferFunctions.hlsli",
            "package/Shaders/Common/FastMath.hlsli",
            "package/Shaders/Common/SharedData.hlsli",
            "package/Shaders/Common/BRDF.hlsli",
            "package/Shaders/Common/GBuffer.hlsli",
            "package/Shaders/Common/LightingCommon.hlsli",
            "package/Shaders/Common/LightingEval.hlsli",
            "package/Shaders/Common/Random.hlsli",
            "package/Shaders/Common/Shading.hlsli",
            "package/Shaders/Common/ShadowSampling.hlsli",
            "package/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli",
            "features/Dynamic Cubemaps/Shaders/DynamicCubemaps",
            "features/Extended Materials/Shaders/ExtendedMaterials",
            "features/IBL/Shaders/IBL/DiffuseIBLCS.hlsl",
            "features/IBL/Shaders/IBL/IBL.hlsli",
            "features/Skylighting/Shaders/Skylighting/Skylighting.hlsli",
            "features/Skylighting/Shaders/Skylighting/UpdateProbesCS.hlsl",
            "features/Screen Space GI/Shaders/ScreenSpaceGI",
            "features/Light Limit Fix/Shaders/LightLimitFix/Common.hlsli",
            "features/Light Limit Fix/Shaders/LightLimitFix/Attenuation.hlsli",
            "features/Rain Refraction/Shaders/RainRefraction/Raindrops.png",
            "features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/RaymarchCS.hlsl",
            "features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli",
            "features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli",
            "features/Terrain Blending/Shaders/TerrainBlending/TerrainBlending.hlsli",
            "features/Terrain Shadows/Shaders/TerrainShadows/ShadowUpdate.cs.hlsl",
            "features/Terrain Shadows/Shaders/TerrainShadows/TerrainShadows.hlsli",
            "features/Terrain Variation/Shaders/TerrainVariation/TerrainVariation.hlsli",
            "features/Upscaling/Shaders/Upscaling/RCAS/RCAS.hlsl",
            "features/Volumetric Shadows/Shaders/VolumetricShadows/BlurShadowCS.hlsl",
            "features/Volumetric Shadows/Shaders/VolumetricShadows/DownsampleShadowCS.hlsl",
            "features/Volumetric Shadows/Shaders/VolumetricShadows/VolumetricShadows.hlsli",
            "features/Water Effects/Shaders/WaterEffects/watercaustics.dds",
            "features/Water Effects/Shaders/WaterEffects/WaterCaustics.hlsli",
            "features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli"
        },
        sources = {
            "src/Profiler.h",
            "src/Profiler.cpp",
            "src/Features/PerformanceOverlay/DrawCallRow.h",
            "src/Features/PerformanceOverlay/CircularBuffer.h",
            "src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.h",
            "src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.cpp",
            "src/Utils/PerfUtils.h",
            "src/Utils/WinApi.h",
            "src/Utils/WinApi.cpp",
            "src/Utils/SphericalHarmonics.h",
            "src/Utils/SphericalHarmonics.cpp",
            "src/Utils/VTableHookFallback.h",
            "src/Utils/VTableHookFallback.cpp",
            "src/Features/ScreenSpaceShadows/bend_sss_cpu.h"
        },
        licenses = {
            "package/SKSE/Plugins/CommunityShaders/Licenses/LICENSE-SebH-MIT.txt"
        },
        includedirs = {
            "src",
            "src/Features/ScreenSpaceShadows"
        }
    }
    local root = path.join(os.projectdir(), shared.root)
    local seen = {}
    local function checked(entry, directory)
        assert(not entry:find("[*?]") and not path.is_absolute(entry)
            and not entry:find("..", 1, true),
            "shared entries must be explicit relative paths: " .. entry)
        local source = path.join(root, entry)
        assert(directory and os.isdir(source) or not directory and os.isfile(source),
            "shared entry missing: " .. entry)
        local key = entry:gsub("\\", "/"):lower()
        assert(not seen[key], "duplicate shared entry: " .. entry)
        seen[key] = true
        return source
    end
    for _, entry in ipairs(shared.shaders) do
        checked(entry, os.isdir(path.join(root, entry)))
    end
    for _, entry in ipairs(shared.sources) do
        local source = checked(entry, false)
        local extension = path.extension(source):lower()
        assert(extension == ".h" or extension == ".hpp" or extension == ".cpp",
            "unsupported shared C++ entry: " .. entry)
        if target then
            target:add(extension == ".cpp" and "files" or "headerfiles", source)
        end
    end
    for _, entry in ipairs(shared.licenses) do
        local source = checked(entry, false)
        if target then
            target:add("installfiles", source,
                { prefixdir = "F4SE/Plugins/FO4CommunityShaders/Licenses" })
        end
    end
    for _, entry in ipairs(shared.includedirs) do
        local directory = checked(entry, true)
        if target then
            target:add("includedirs", directory)
        end
    end
    if target then
        target:add("sysincludedirs", path.join(os.projectdir(), "extern/Streamline/external/json/include"))
    end
    return shared
end
