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

    local function shared_destination(source)
        return source:gsub("\\", "/"):match("^package/Shaders/(.+)$")
            or source:gsub("\\", "/"):match("^features/[^/]+/Shaders/(.+)$")
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

    local shader_directories = { "package/Shaders" }
    for _, directory in ipairs(os.dirs(path.join(root, "features/*/Shaders"))) do
        table.insert(shader_directories, path.relative(directory, root))
    end
    table.sort(shader_directories)
    for _, directory in ipairs(shader_directories) do
        local source_root = path.join(root, directory)
        for _, source in ipairs(os.files(path.join(source_root, "**"))) do
            register(source, path.relative(source, source_root))
        end
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
