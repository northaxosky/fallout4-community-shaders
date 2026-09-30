function main(target)
    local shared = {
        root = "extern/community-shaders-shared",
        shaders = {
            "package/Shaders/Common/FrameBuffer.hlsli",
            "package/Shaders/Common/Math.hlsli",
            "package/Shaders/Common/SharedData.hlsli",
            "package/Shaders/Common/Spherical Harmonics/SphericalHarmonics.hlsli",
            "package/Shaders/LICENSE",
            "package/Shaders/Common/Spherical Harmonics/LICENSE",
            "features/Dynamic Cubemaps/Shaders/DynamicCubemaps/defaultcubemap.dds",
            "features/Screen Space GI/Shaders/ScreenSpaceGI/fast_2uges.dds",
            "features/Terrain Shadows/Shaders/TerrainShadows/ShadowUpdate.cs.hlsl",
            "features/Terrain Shadows/Shaders/TerrainShadows/TerrainShadows.hlsli",
            "features/Upscaling/Shaders/Upscaling/RCAS/RCAS.hlsl",
            "features/Water Effects/Shaders/WaterEffects/watercaustics.dds"
        },
        sources = {
            "src/Utils/PerfUtils.h",
            "src/Features/ScreenSpaceShadows/bend_sss_cpu.h"
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
    for _, entry in ipairs(shared.includedirs) do
        local directory = checked(entry, true)
        if target then
            target:add("includedirs", directory)
        end
    end
    return shared
end
