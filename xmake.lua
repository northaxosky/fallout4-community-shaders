set_xmakever("3.0.0")

set_config("commonlib_xbyak", true)

-- CommonLibF4 owns the plugin, install, and package rules.
includes("extern/CommonLibF4")

local plugin_name = "FO4CommunityShaders"
local plugin_version = "0.1.0"
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
    "ScreenSpaceGI",
    "InverseSquareLighting",
    "ExponentialHeightFog",
    "DynamicCubemaps",
    "WetnessEffects",
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
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.project-manifest.bin",
    "features/Upscaling/Shaders/Upscaling/Streamline/sl.project-manifest.sig",
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
    after_package(function(target)
        import("xmake.release_package", { rootdir = os.projectdir() }).main(target)
    end)
end)

includes("xmake/streamline.lua")
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
    add_deps(plugin_name .. "Version", "Streamline")

    add_files(
        "src/**.cpp",
        "features/*/src/**.cpp"
    )

    add_headerfiles("src/**.h", "features/*/src/**.h")
    add_includedirs(generated_include, "src", "extern")
    for _, feature in ipairs(features) do
        add_includedirs(path.join("features", feature, "src"))
    end
    add_includedirs(
        "features/Upscaling/src/RCAS",
        "extern/RenderDoc/include"
    )

    add_packages(
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

    add_linkdirs("extern/detours/Release")
    add_links("detours")
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

    add_installfiles("package/(**)", { prefixdir = "." })
    for _, feature in ipairs(features) do
        if feature == "Upscaling" then
            add_installfiles(
                "features/Upscaling/(Shaders/Upscaling/*.hlsl)",
                "features/Upscaling/(Shaders/Upscaling/RCAS/**)",
                "features/Upscaling/(Shaders/Upscaling/Streamline/**)",
                { prefixdir = "." }
            )
        else
            add_installfiles(
                path.join("features", feature, "(Shaders/**)"),
                { prefixdir = "." }
            )
        end
    end
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
        "src/Settings/FeatureConfig.cpp"
    )
    add_headerfiles(
        "src/Settings/FeatureConfig.h",
        "src/Settings/FeatureKeys.h"
    )
    add_packages("vcpkg::tomlplusplus")
end)

target("TemporalPipelineStateTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/TemporalPipelineStateTests.cpp")
    add_headerfiles(
        "src/Render/TemporalPipelineState.h",
        "src/Render/TemporalRenderSizing.h"
    )
end)

target("TerrainShadowsMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/TerrainShadowsMathTests.cpp")
    add_headerfiles("features/TerrainShadows/src/TerrainShadowsMath.h")
    add_includedirs("features/TerrainShadows/src")
end)

target("TerrainShadowsResizeTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/TerrainShadowsResizeTests.cpp",
        "features/TerrainShadows/src/HeightMapResize.cpp"
    )
    add_headerfiles("features/TerrainShadows/src/HeightMapResize.h")
    add_includedirs("features/TerrainShadows/src")
    add_packages("vcpkg::directxtex")
    add_syslinks("ole32")
end)

target("WetnessEffectsMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/WetnessEffectsMathTests.cpp")
    add_headerfiles("features/WetnessEffects/src/WetnessMath.h")
    add_includedirs("features/WetnessEffects/src")
end)

target("InverseSquareLightingMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/InverseSquareLightingMathTests.cpp")
    add_headerfiles(
        "features/InverseSquareLighting/src/InverseSquareLightingMath.h"
    )
    add_includedirs("features/InverseSquareLighting/src")
end)

target("ExponentialHeightFogMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/ExponentialHeightFogMathTests.cpp")
    add_headerfiles(
        "features/ExponentialHeightFog/src/ExponentialHeightFogMath.h"
    )
    add_includedirs("features/ExponentialHeightFog/src")
end)

target("WaterEffectsMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/WaterEffectsMathTests.cpp")
    add_headerfiles("features/WaterEffects/src/WaterEffectsMath.h")
    add_includedirs("features/WaterEffects/src")
end)

target("StreamlineModuleFixture", function()
    set_kind("shared")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "build", "tests"))
    add_files("tests/StreamlineModuleFixture.cpp")
end)

target("StreamlineModuleTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/StreamlineModuleTests.cpp",
        "src/Utils/StreamlineModule.cpp"
    )
    add_deps("Streamline", "StreamlineModuleFixture")
    add_syslinks("psapi")
end)

target("UpscalingPublicationTests", function()
    set_kind("binary")
    set_default(false)
    add_rules("fo4cs.directxtk")
    add_deps(plugin_name .. "Version", "commonlibf4", "ShaderStage")
    add_files(
        "tests/UpscalingPublicationTests.cpp",
        "src/Render/Annotation.cpp",
        "src/Render/RendererContext.cpp"
    )
    add_headerfiles(
        "features/Upscaling/src/UpscalingPublication.h",
        "src/Render/Annotation.h",
        "src/Render/RendererContext.h"
    )
    add_includedirs(
        generated_include,
        "extern",
        "tests",
        "extern/CommonLibF4/include",
        "extern/CommonLibF4/lib/commonlib-shared/include",
        "features/Upscaling/src"
    )
    add_packages(
        "vcpkg::directx-headers",
        "vcpkg::directxtk",
        "vcpkg::magic-enum",
        "vcpkg::tomlplusplus"
    )
    add_defines(
        "_WINDOWS",
        "_AMD64_",
        "_UNICODE",
        "COMMONLIB_RUNTIMECOUNT=3"
    )
    add_syslinks("bcrypt", "d3dcompiler", "d3d11", "dxguid")
    add_linkdirs("extern/detours/Release")
    add_links("detours")
    set_pcxxheader("src/PCH.h")
end)

target("ProjectionMathTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/ProjectionMathTests.cpp")
    add_deps("commonlibf4")
    add_packages("vcpkg::directxmath")
end)

target("FrameBufferTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/FrameBufferTests.cpp")
    add_headerfiles("src/Render/FrameBufferMath.h")
    add_packages("vcpkg::directxmath")
end)

target("FrameGenerationContractTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/FrameGenerationContractTests.cpp")
    add_headerfiles(
        "src/Render/FrameGenerationOrchestration.h",
        "src/Render/TemporalPipelineState.h"
    )
    add_includedirs(
        "features/FrameGeneration/src",
        "extern",
        "extern/Streamline/include"
    )
end)

target("DXGISwapChainProxyTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/DXGISwapChainProxyTests.cpp",
        "features/Upscaling/src/DXGISwapChainProxy.cpp"
    )
    add_includedirs("features/Upscaling/src")
    add_syslinks("d3d11", "dxgi", "ole32")
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

target("ScreenSpaceGIHistoryTests", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/ScreenSpaceGIHistoryTests.cpp")
    add_headerfiles(
        "features/ScreenSpaceGI/src/ScreenSpaceGIHistory.h"
    )
    add_includedirs("features/ScreenSpaceGI/src")
end)

target("ShaderCompileTests", function()
    set_kind("binary")
    set_default(false)
    add_deps("ShaderStage")
    add_files(
        "tests/ShaderCompileTests.cpp",
        "src/Utils/ShaderCompile.cpp"
    )
    add_headerfiles("src/Utils/ShaderCompile.h")
    add_packages("vcpkg::directx-headers")
    add_syslinks("d3dcompiler")
end)

target("PixelShaderSwapTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/PixelShaderSwapTests.cpp",
        "src/Render/PixelShaderSwapModel.cpp",
        "src/Render/ShaderVariantResolver.cpp"
    )
    add_headerfiles(
        "src/Render/PixelShaderSwapBroker.h",
        "src/Render/ShaderVariantResolver.h"
    )
end)

target("ShaderCacheTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/ShaderCacheTests.cpp",
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
    add_syslinks("bcrypt", "d3dcompiler", "version")
end)

target("ShaderVariantCompilationTests", function()
    set_kind("binary")
    set_default(false)
    add_files(
        "tests/ShaderVariantCompilationTests.cpp",
        "src/Render/ShaderVariantCompilation.cpp"
    )
    add_syslinks("d3d11")
end)

target("ShaderInjectionRegistrationTests", function()
    set_kind("binary")
    set_default(false)
    add_rules("fo4cs.directxtk")
    add_deps(plugin_name .. "Version", "commonlibf4")
    add_files(
        "tests/ShaderInjectionRegistrationTests.cpp",
        "src/Render/ShaderInjection.cpp",
        "src/Render/ShaderInjectionCompileRequest.cpp",
        "src/Render/ShaderFamilyDescriptor.cpp",
        "src/Render/SharedDataDispatchScope.cpp",
        "src/Render/PixelShaderSwapBroker.cpp",
        "src/Render/PixelShaderSwapModel.cpp",
        "src/Render/ShaderSubclassContext.cpp",
        "src/Render/ShaderVariantResolver.cpp",
        "src/Render/ShaderVariantRuntimeResolver.cpp"
    )
    add_includedirs(
        generated_include,
        "extern",
        "tests",
        "extern/CommonLibF4/include",
        "extern/CommonLibF4/lib/commonlib-shared/include"
    )
    add_packages(
        "vcpkg::directx-headers",
        "vcpkg::directxtk",
        "vcpkg::magic-enum",
        "vcpkg::tomlplusplus"
    )
    add_defines(
        "_WINDOWS",
        "_AMD64_",
        "_UNICODE",
        "COMMONLIB_RUNTIMECOUNT=3",
        "FO4CS_SHADER_INJECTION_TESTING"
    )
    add_syslinks("bcrypt", "d3dcompiler", "d3d11")
    add_linkdirs("extern/detours/Release")
    add_links("detours")
    set_pcxxheader("src/PCH.h")
end)

target("FeatureConfigTests", function()
    add_tests("FeatureConfig")
end)

target("TemporalPipelineStateTests", function()
    add_tests("TemporalPipelineState")
end)

target("TerrainShadowsMathTests", function()
    add_tests("TerrainShadowsMath")
end)

target("TerrainShadowsResizeTests", function()
    add_tests("TerrainShadowsResize")
end)

target("WetnessEffectsMathTests", function()
    add_tests("WetnessEffectsMath")
end)

target("InverseSquareLightingMathTests", function()
    add_tests("InverseSquareLightingMath")
end)

target("ExponentialHeightFogMathTests", function()
    add_tests("ExponentialHeightFogMath")
end)

target("WaterEffectsMathTests", function()
    add_tests("WaterEffectsMath")
end)

target("StreamlineModuleTests", function()
    add_tests("StreamlineModule", {
        runargs = path.join(
            os.projectdir(),
            "build/tests/StreamlineModuleFixture.dll"
        )
    })
end)

target("UpscalingPublicationTests", function()
    add_tests("UpscalingPublication", {
        runargs = {
            path.join(
                os.projectdir(),
                "features/Upscaling/Shaders/Upscaling/SpatialFallbackPS.hlsl"
            ),
            path.join(
                os.projectdir(),
                "features/Upscaling/Shaders/Upscaling/UpscaleVS.hlsl"
            ),
            path.join(
                os.projectdir(),
                "build/ShaderStage/Shaders/Upscaling/EncodeTexturesCS.hlsl"
            )
        }
    })
end)

target("ProjectionMathTests", function()
    add_tests("ProjectionMath")
end)

target("FrameBufferTests", function()
    add_tests("FrameBuffer")
end)

target("FrameGenerationContractTests", function()
    add_tests("FrameGenerationContract")
end)

target("DXGISwapChainProxyTests", function()
    add_tests("DXGISwapChainProxy")
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

target("ScreenSpaceGIHistoryTests", function()
    add_tests("ScreenSpaceGIHistory")
end)

target("ShaderCompileTests", function()
    add_tests("ShaderCompile", {
        runargs = path.join(os.projectdir(), "build/ShaderStage/Shaders")
    })
end)

target("PixelShaderSwapTests", function()
    add_tests("PixelShaderSwap")
end)

target("ShaderCacheTests", function()
    add_tests("ShaderCache")
end)

target("ShaderVariantCompilationTests", function()
    add_tests("ShaderVariantCompilation")
end)

target("ShaderInjectionRegistrationTests", function()
    add_tests("ShaderInjectionClaimLedger", {
        runargs = "--claim-ledger"
    })
    add_tests("ShaderInjectionComputeHooksMissing", {
        runargs = "--compute-hooks-missing"
    })
    add_tests("ShaderInjectionComputePhase", {
        runargs = "--compute-phase"
    })
    add_tests("ShaderInjectionContributorConflict", {
        runargs = "--contributor-conflict"
    })
end)
