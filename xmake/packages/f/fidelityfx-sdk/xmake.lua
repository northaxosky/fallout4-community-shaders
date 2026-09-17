package("fidelityfx-sdk")
    set_homepage("https://gpuopen.com/fidelityfx-sdk/")
    set_description("Local FidelityFX SDK DX11 backend and FSR 3 host API")
    set_license("MIT")
    set_policy("package.cmake_generator.ninja", false)

    add_deps("cmake")
    add_includedirs("include")
    add_linkdirs("lib")
    add_links(
        "ffx_backend_dx11_x64",
        "ffx_fsr3_x64",
        "ffx_fsr3upscaler_x64",
        "ffx_opticalflow_x64",
        "ffx_frameinterpolation_x64"
    )

    on_install("windows|x64", function(package)
        import("package.tools.cmake")

        local root = path.absolute(path.join(package:scriptdir(), "../../../.."))
        local sdk = path.join(root, "extern", "FidelityFX-SDK", "sdk")
        assert(
            os.isfile(path.join(sdk, "CMakeLists.txt")),
            "FidelityFX-SDK submodule is not initialized"
        )

        local builddir = path.join(root, "build", "FidelityFX-SDK-xmake")
        local output = path.join(builddir, "output")
        local configs = {
            "-DFFX_API_CUSTOM=ON",
            "-DFFX_API_DX11=ON",
            "-DFFX_API_DX12=OFF",
            "-DFFX_API_VK=OFF",
            "-DFFX_ALL=OFF",
            "-DFFX_FSR=ON",
            "-DFFX_FSR3=ON",
            "-DFFX_AUTO_COMPILE_SHADERS=ON",
            "-DFFX_PLATFORM_NAME=x64",
            "-DBIN_OUTPUT=" .. output
        }

        local oldir = os.cd(sdk)
        cmake.build(package, configs, {
            cmake_build = true,
            cmake_generator = "Visual Studio",
            builddir = builddir,
            config = "Release",
            target = {
                "ffx_backend_dx11_x64",
                "ffx_fsr3_x64"
            }
        })
        os.cd(oldir)

        for _, library in ipairs(
            os.files(path.join(output, "ffx_sdk", "**.lib"))
        ) do
            os.cp(
                library,
                path.join(package:installdir("lib"), path.filename(library))
            )
        end
        os.cp(path.join(sdk, "include", "**"), package:installdir("include"))
    end)

    on_test(function(package)
        assert(package:has_cxxincludes(
            "FidelityFX/host/ffx_fsr3.h",
            { configs = { languages = "c++17" } }
        ))
    end)
