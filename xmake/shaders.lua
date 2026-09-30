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
    "tests/shaders/SharedDataProbe.hlsl",
    "tests/shaders/SSSConsumerProbe.hlsl"
}

local root = os.projectdir()

local function excluded(relative)
    -- SDK runtimes keep their manifest-owned install path.
    return relative == "Upscaling/XeSS"
        or relative:startswith("Upscaling/XeSS/")
        or relative == "Upscaling/FidelityFX"
        or relative:startswith("Upscaling/FidelityFX/")
        or relative == "Upscaling/Streamline"
        or relative:startswith("Upscaling/Streamline/")
end

local function stage_shaders()
    local shared = import("xmake.shared", { rootdir = root }).main()
    local shared_root = path.join(root, shared.root)
    local destination = path.absolute(
        path.join(root, "build", "ShaderStage", "Shaders")
    )
    local staged = {}
    local upstream = {}

    local function shared_destination(source)
        return source:gsub("\\", "/"):match("^package/Shaders/(.+)$")
            or source:gsub("\\", "/"):match("^features/[^/]+/Shaders/(.+)$")
    end

    local upstream_roots = { path.join(shared_root, "package/Shaders") }
    table.join2(upstream_roots, os.dirs(path.join(shared_root, "features/*/Shaders")))
    assert(os.isdir(upstream_roots[1]),
        "shared shaders missing; run git submodule update --init --recursive")
    for _, directory in ipairs(upstream_roots) do
        for _, source in ipairs(os.files(path.join(directory, "**"))) do
            local relative = path.relative(source, directory):gsub("\\", "/")
            upstream[relative:lower()] = source
        end
    end

    local function register(source, relative, from_shared)
        relative = relative:gsub("\\", "/")
        local key = relative:lower()
        if from_shared then
            assert(path.extension(source):lower() ~= ".dll",
                "shared DLLs must not be staged: " .. source)
            assert(not excluded(relative),
                "shared consumption entry is excluded from staging: " .. source)
        end
        if excluded(relative) then
            return
        end
        if not from_shared then
            assert(not upstream[key],
                "FO4 shader shadows upstream destination '" .. relative
                .. "'; move it under FO4/: " .. source)
        end

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
    for _, entry in ipairs(shared.shaders) do
        local source = path.join(shared_root, entry)
        local files = os.isdir(source) and os.files(path.join(source, "**")) or { source }
        for _, file in ipairs(files) do
            local relative = shared_destination(path.relative(file, shared_root))
            assert(relative,
                "shared shader must be under package/Shaders or features/*/Shaders: " .. entry)
            register(file, relative, true)
        end
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
