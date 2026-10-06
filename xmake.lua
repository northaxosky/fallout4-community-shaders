set_xmakever("3.0.0")

set_config("commonlib_xbyak", true)

-- CommonLibF4 owns the plugin, install, and package rules.
includes("extern/CommonLibF4")

local plugin_name = "FO4CommunityShaders"
local plugin_version = "0.3.1"
local plugin_version_major, plugin_version_minor, plugin_version_patch =
    plugin_version:match("^(%d+)%.(%d+)%.(%d+)$")

local features = {
    "RenderDoc",
    "PerformanceOverlay",
    "MotionVectorFixes",
    "Upscaling",
    "FrameGeneration",
    "ScreenSpaceShadows",
    "TerrainShadows",
    "LODBlending",
    "TerrainVariation",
    "ScreenSpaceGI",
    "InverseSquareLighting",
    "ExponentialHeightFog",
    "DynamicCubemaps",
    "WaterEffects"
}

set_project(plugin_name)
set_version(plugin_version)
set_license("GPL-3.0-or-later")
-- MSVC's C++23 mode exposes a broken branch in the pinned Streamline headers.
set_languages("c++latest")
set_toolchains("msvc")
set_arch("x64")
set_warnings("allextra", "error")
set_encodings("utf-8")
set_runtimes("MD")
add_cxxflags(
    "/EHsc",
    "/permissive-",
    "/Zc:__cplusplus",
    "/Zc:preprocessor",
    "/arch:AVX",
    { force = true }
)
add_ldflags("/WX", { force = true })

add_rules("mode.release", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

option("tracy", function()
    set_default(false)
    set_description("Enable Tracy profiler instrumentation")
end)

option("release_identity", function()
    set_default("")
    set_showmenu(true)
    set_description("Release tag embedded in the build and package filename")
end)

task("release-info", function()
    set_category("plugin")
    on_run("xmake.release_info")
    set_menu {
        usage = "xmake release-info [options]",
        description = "Compute release metadata for the checked-out commit",
        options = {
            { nil, "before", "kv", nil, "Previous push commit" },
            { nil, "dispatch", "k", nil, "Select the stable release channel" },
            { nil, "no-publish", "k", nil, "Build without publishing" },
            { nil, "output", "kv", nil, "Append metadata to this output file" }
        }
    }
end)

add_repositories("fo4cs-repository xmake")

add_requires("vcpkg::detours 4.0.1")
add_requires("vcpkg::directx-headers 1.619.1")
add_requires("vcpkg::directxmath")
add_requires("vcpkg::directxtex 2025-10-27")
add_requires("vcpkg::directxtk 2025-10-27")
add_requires("vcpkg::magic-enum 0.9.7")
add_requires("vcpkg::tomlplusplus 3.4.0")

if has_config("tracy") then
    add_requires("vcpkg::tracy 0.13.1", {
        configs = { features = { "on-demand" } }
    })
end

rule("fo4cs.directxtk", function()
    on_config(function(target)
        local package = target:pkg("vcpkg::directxtk")
        local includedirs = table.wrap(
            package:get("sysincludedirs") or package:get("includedirs")
        )
        target:add("includedirs", path.join(includedirs[1], "directxtk"))
    end)
end)

local required_sdk_assets = {
    "features/Upscaling/Shaders/Upscaling/Streamline/nvngx_dlss.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/nvngx_dlssg.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.interposer.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.common.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.dlss.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.dlss_g.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.fsr.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.fsr_g.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.pcl.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.reflex.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/amd_fidelityfx_framegeneration_dx12.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/amd_fidelityfx_upscaler_dx12.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/cs_fidelityfx_framegeneration_dx12.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/cs_fidelityfx_upscaler_dx12.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/D3D12/D3D12Core.dll",
    "features/Upscaling/Shaders/Upscaling/Streamline/D3D12/LICENSE.txt",
    "features/Upscaling/Shaders/Upscaling/Streamline/license.txt",
    "features/Upscaling/Shaders/Upscaling/Streamline/3rd-party-licenses.md",
    "features/Upscaling/Shaders/Upscaling/Streamline/amd-fidelityfx-license.md",
    "features/Upscaling/Shaders/Upscaling/Streamline/amd-fidelityfx-third-party-notices.md",
    "features/Upscaling/Shaders/Upscaling/Streamline/nvngx_dlss.license.txt",
    "features/Upscaling/Shaders/Upscaling/Streamline/reflex.license.txt"
}

rule("fo4cs.sdk-assets", function()
    before_package(function()
        for _, asset in ipairs(required_sdk_assets) do
            assert(
                os.isfile(path.join(os.projectdir(), asset)),
                "Missing required SDK package asset: " .. asset
            )
        end
    end)
end)

rule("fo4cs.release-package", function()
    on_config(function(target)
        -- Mod managers and Nexus expect F4SE\ and Shaders\ at the archive root.
        target:data_set("commonlib.plugin.package", { prefixdir = "" })
    end)
    after_package(function(target)
        import("xmake.release_package", { rootdir = os.projectdir() }).main(target)
    end)
end)

includes("xmake/shaders.lua")

local generated_include = path.join("build", ".gens", plugin_name)

target(plugin_name .. "Version", function()
    set_kind("phony")
    set_default(false)
    set_policy("build.fence", true)

    on_prepare(function()
        import("core.project.config")

        local describe = "unknown"
        local sha = "unknown"
        if os.isfile(".git") or os.isdir(".git") then
            describe = os.iorunv(
                "git",
                { "describe", "--tags", "--dirty", "--always" }
            ):trim()
            sha = os.iorunv("git", { "rev-parse", "HEAD" }):trim()
        end
        local identity = config.get("release_identity") or ""
        if identity ~= "" then
            assert(identity:match("^v%d+%.%d+%.%d+$") or identity:match("^v%d+%.%d+%.%d+%-dev%.%d+$"),
                "release_identity must be a stable or dev release tag")
            describe = identity
        end
        local content = io.readfile("xmake/Plugin.h.in")
        local variables = {
            BUILD_DESCRIBE = describe,
            BUILD_GIT_SHA = sha,
            PROJECT_NAME = plugin_name,
            PROJECT_VERSION_MAJOR = plugin_version_major,
            PROJECT_VERSION_MINOR = plugin_version_minor,
            PROJECT_VERSION_PATCH = plugin_version_patch
        }

        for name, value in pairs(variables) do
            content = content:gsub("@" .. name .. "@", value)
        end

        local output = path.join(generated_include, "Plugin.h")
        if not os.isfile(output) or io.readfile(output) ~= content then
            os.mkdir(path.directory(output))
            io.writefile(output, content)
        end
    end)
end)

target(plugin_name, function()
    set_kind("shared")

    add_rules("commonlibf4.plugin", {
        name = plugin_name,
        author = "northaxosky",
        description = "Community Shaders for Fallout 4",
        plugin_template = "xmake/commonlibf4-plugin.cpp.in"
    })
    add_rules("fo4cs.directxtk", "fo4cs.sdk-assets", "fo4cs.release-package")
    add_deps(plugin_name .. "Version")

    add_files(
        "src/**.cpp",
        "features/*/src/**.cpp"
    )
    add_files("src/Menu/Changelog.rc", { includedirs = os.projectdir() })

    before_build(function(target)
        local changelog = path.join(os.projectdir(), "CHANGELOG.md")
        local resource_object = target:objectfile("src/Menu/Changelog.rc")

        -- The resource rule does not track RCDATA payload files.
        if os.isfile(resource_object) and os.mtime(changelog) > os.mtime(resource_object) then
            os.rm(resource_object)
        end
    end)

    add_headerfiles("src/**.h", "features/*/src/**.h")
    add_includedirs(generated_include, "src", "extern")
    on_load(function(target)
        import("xmake.shared", { rootdir = os.projectdir() }).main(target)
    end)
    for _, feature in ipairs(features) do
        add_includedirs(path.join("features", feature, "src"))
    end
    add_includedirs(
        "features/Upscaling/src/RCAS",
        "extern/RenderDoc/include",
        "extern/Streamline/include"
    )

    add_packages(
        "vcpkg::detours",
        "vcpkg::directx-headers",
        "vcpkg::directxmath",
        "vcpkg::directxtex",
        "vcpkg::directxtk",
        "vcpkg::magic-enum",
        "vcpkg::tomlplusplus"
    )
    if has_config("tracy") then
        add_packages("vcpkg::tracy")
        add_defines("TRACY_SUPPORT", "TRACY_ENABLE")
    end

    add_syslinks(
        "bcrypt",
        "d3dcompiler",
        "d3d12",
        "dxgi",
        "ole32",
        "oleaut32",
        "version"
    )

    add_defines(
        "_WINDOWS",
        "_AMD64_",
        "_UNICODE",
        "COMMONLIB_RUNTIMECOUNT=3"
    )
    if is_mode("release", "releasedbg") then
        set_policy("build.optimization.lto", true)
    end

    set_pcxxheader("src/PCH.h")

    add_deps("ShaderStage")
    add_installfiles("package/(**)|Shaders/**", { prefixdir = "." })
    add_installfiles("build/ShaderStage/(Shaders/**)|SharedDataProbe.hlsl",
        { prefixdir = "." })
    add_installfiles("features/Upscaling/(Shaders/Upscaling/Streamline/**)",
        { prefixdir = "." })
    add_installfiles("LICENSE", "EXCEPTIONS.md", { prefixdir = "." })
end)

-- Host tests remain ordinary, non-default C++ executable targets.
add_includedirs(
    "src",
    "extern/CommonLibF4/lib/dearmoddingui-api/include"
)

target("FeatureConfigTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/FeatureConfigTests.cpp",
        "features/InverseSquareLighting/src/LightAuthoring.cpp",
        "src/Settings/FeatureConfig.cpp"
    )
    add_headerfiles(
        "src/Settings/FeatureConfig.h",
        "src/Settings/FeatureKeys.h"
    )
    for _, feature in ipairs(features) do
        add_includedirs("features/" .. feature .. "/src")
    end
    add_packages("vcpkg::tomlplusplus")
end)

target("RenderDocCaptureServiceTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/RenderDocCaptureServiceTests.cpp",
        "features/RenderDoc/src/CaptureService.cpp"
    )
    add_includedirs("features/RenderDoc/src", "extern/RenderDoc/include")
    add_tests("RenderDocCaptureService", {
        runargs = { path.join(os.projectdir(), "build/RenderDocCaptureServiceTests") }
    })
end)

target("FrameGenerationRetirementGpuTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/FrameGenerationRetirementGpuTests.cpp",
        "features/Upscaling/src/AgilityBootstrap.cpp"
    )
    add_headerfiles("features/Upscaling/src/AgilityBootstrap.h")
    add_includedirs(
        generated_include,
        "src",
        "extern",
        "features/Upscaling/src"
    )
    add_packages("vcpkg::directx-headers")
    add_syslinks("d3d11", "d3d12", "dxgi", "ole32", "version")
end)

-- One gate binary per compiler: d3dcompiler_47.dll resolves from the executable directory.
-- A nil compiler selects the pinned Windows SDK copy; otherwise it is a project-relative path.
local function stock_shader_identity_target(name, compiler, tests)
    target(name, function()
        set_kind("binary")
        set_default(false)
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/" .. name)
        add_deps("ShaderStage")
        add_files(
            "tests/StockShaderIdentityTests.cpp",
            "tests/ShaderABIChecks.cpp",
            "src/Render/ShaderFamilyDescriptor.cpp",
            "src/Render/ShaderInjectionCompileRequest.cpp",
            "src/Render/ShaderVariantRecipe.cpp",
            "src/Utils/CSSha1.cpp",
            "src/Utils/CSSha256.cpp",
            "src/Utils/ShaderCompile.cpp",
            "src/Utils/ShaderCache/CacheStorage.cpp",
            "src/Utils/ShaderCache/CompilerIdentity.cpp",
            "src/Utils/ShaderCache/DependencyTrace.cpp",
            "src/Utils/ShaderCache/RevalidationContext.cpp",
            "src/Utils/ShaderCache/ShaderRecipe.cpp",
            "src/Utils/ShaderCache/SourceCompile.cpp"
        )
        add_includedirs("features/ScreenSpaceGI/src")
        add_packages("spdlog", "vcpkg::directxmath", "vcpkg::tomlplusplus")
        add_syslinks("bcrypt", "d3dcompiler", "version")
        set_values("fo4cs.identity_compiler", compiler)
        after_build(function(target)
            local source = target:values("fo4cs.identity_compiler")
            if source then
                source = path.join(os.projectdir(), source)
                assert(os.isfile(source), "Stock shader identity requires " .. source .. "; run scripts\\fetch-sdks.ps1")
            else
                local vcvars = target:toolchain("msvc"):config("vcvars")
                local sdkdir = os.getenv("WindowsSdkDir") or (vcvars and vcvars.WindowsSdkDir)
                assert(sdkdir, "Cannot locate Windows SDK for the pinned shader compiler")
                source = path.join(sdkdir, "bin", "10.0.26100.0", "x64", "d3dcompiler_47.dll")
                assert(os.isfile(source), "Stock shader identity requires SDK bin/10.0.26100.0/x64/d3dcompiler_47.dll")
            end
            os.cp(source, target:targetdir())
        end)
        for test, spec in pairs(tests) do
            add_tests(test, {
                runargs = {
                    path.join(os.projectdir(), "build/ShaderStage/Shaders"),
                    path.join(os.projectdir(), "tests/data", spec.file),
                    spec.features and "--features-on" or nil
                },
                run_timeout = 600000
            })
        end
    end)
end

stock_shader_identity_target("StockShaderIdentityTests", nil, {
    StockShaderIdentity = { file = "stock-shader-identity.tsv" },
    FeatureShaderCompile = { file = "stock-shader-identity.tsv", features = true },
    -- OG recipes compile at runtime with the system compiler, so feature coverage uses it too.
    FeatureShaderCompileOG = { file = "stock-shader-identity-OG-1.10.163.tsv", features = true }
})

-- The compiler OG 1.10.163 shipped with; staged by scripts\fetch-sdks.ps1, never packaged.
stock_shader_identity_target("StockShaderIdentityOGTests", "build/tools/d3dcompiler-6.3.9600.16384/d3dcompiler_47.dll", {
    StockShaderIdentityOG = { file = "stock-shader-identity-OG-1.10.163.tsv" }
})

target("ShaderCacheTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/ShaderCacheTests.cpp",
        "src/Render/ShaderVariantCompilation.cpp",
        "src/Utils/CSSha256.cpp",
        "src/Utils/ShaderCompile.cpp",
        "src/Utils/ShaderCache/CacheRecord.cpp",
        "src/Utils/ShaderCache/CacheStorage.cpp",
        "src/Utils/ShaderCache/CompilerIdentity.cpp",
        "src/Utils/ShaderCache/DependencyTrace.cpp",
        "src/Utils/ShaderCache/RevalidationContext.cpp",
        "src/Utils/ShaderCache/ShaderCache.cpp",
        "src/Utils/ShaderCache/ShaderRecipe.cpp",
        "src/Utils/ShaderCache/SourceCompile.cpp"
    )
    add_headerfiles(
        "src/Utils/CSSha256.h",
        "src/Utils/ShaderCompile.h",
        "src/Utils/ShaderCache/ByteCodec.h",
        "src/Utils/ShaderCache/CacheRecord.h",
        "src/Utils/ShaderCache/CacheStorage.h",
        "src/Utils/ShaderCache/CompilerIdentity.h",
        "src/Utils/ShaderCache/DependencyTrace.h",
        "src/Utils/ShaderCache/RevalidationContext.h",
        "src/Utils/ShaderCache/ShaderCache.h",
        "src/Utils/ShaderCache/ShaderRecipe.h",
        "src/Utils/ShaderCache/SourceCompile.h"
    )
    add_syslinks("bcrypt", "d3dcompiler", "d3d11", "version")
end)

target("FeatureConfigTests", function()
    add_tests("FeatureConfig")
end)

target("FrameGenerationRetirementGpuTests", function()
    add_tests("FrameGenerationRetirementGpu")
    add_tests("AgilityBootstrapGpu", {
        runargs = {
            "--agility",
            path.join(
                os.projectdir(),
                "features/Upscaling/Shaders/Upscaling/Streamline/D3D12"
            )
        }
    })
end)

target("ShaderCacheTests", function()
    add_tests("ShaderCache")
end)

