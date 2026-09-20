local shader_directories = {
    "package/Shaders",
    "features/ScreenSpaceGI/Shaders",
    "features/ScreenSpaceShadows/Shaders",
    "features/TerrainShadows/Shaders",
    "features/Upscaling/Shaders",
    "features/FrameGeneration/Shaders",
    "features/InverseSquareLighting/Shaders",
    "features/ExponentialHeightFog/Shaders",
    "features/DynamicCubemaps/Shaders",
    "features/WetnessEffects/Shaders",
    "features/WaterEffects/Shaders"
}

local shader_files = {
    "tests/shaders/SharedDataProbe.hlsl"
}

local root = os.projectdir()

local function excluded(relative)
    return relative == "Upscaling/XeSS"
        or relative:startswith("Upscaling/XeSS/")
end

local function stage_shaders()
    local destination = path.absolute(
        path.join(root, "build", "ShaderStage", "Shaders")
    )
    local staged = {}

    local function register(source, relative)
        relative = relative:gsub("\\", "/")
        if excluded(relative) then
            return
        end

        local key = relative:lower()
        if staged[key] then
            raise(
                "shader stage duplicate destination '%s': '%s' and '%s'",
                relative,
                staged[key].source,
                source
            )
        end
        staged[key] = {
            relative = relative,
            source = source
        }
    end

    for _, directory in ipairs(shader_directories) do
        local source_root = path.join(root, directory)
        for _, source in ipairs(os.files(path.join(source_root, "**"))) do
            register(source, path.relative(source, source_root))
        end
    end
    for _, source in ipairs(shader_files) do
        register(path.join(root, source), path.filename(source))
    end

    os.tryrm(destination)
    for _, entry in pairs(staged) do
        local output = path.join(destination, entry.relative)
        os.mkdir(path.directory(output))
        os.cp(entry.source, output)
    end
end

target("ShaderStage", function()
    set_kind("phony")
    set_default(false)
    on_build(stage_shaders)
end)
