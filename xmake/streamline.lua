local root = os.projectdir()
local trust_header = path.join(
    root,
    "build",
    ".gens",
    "Streamline",
    "projectTrust.generated.h"
)

target("Streamline", function()
    set_kind("static")
    set_default(false)

    add_files(
        path.join(
            root,
            "extern/Streamline/source/core/sl.security/projectTrust.cpp"
        ),
        path.join(
            root,
            "extern/Streamline/source/core/sl.security/physicalFilePath.cpp"
        )
    )
    add_includedirs(
        path.join(root, "extern/Streamline/include"),
        path.join(root, "extern/Streamline"),
        { public = true }
    )
    add_defines(
        "NOMINMAX",
        'SL_PROJECT_TRUST_CONFIG_HEADER="' .. trust_header:gsub("\\", "/") .. '"'
    )
    add_cxxflags("/wd5103", { force = true })
    add_syslinks("bcrypt", "psapi", { public = true })

    before_build(function()
        import("lib.detect.find_tool")

        local pwsh = assert(
            find_tool("pwsh"),
            "PowerShell 7 (pwsh) is required to generate the Streamline trust header"
        )
        os.mkdir(path.directory(trust_header))
        os.vrunv(pwsh.program, {
            "-NoProfile",
            "-File",
            path.join(root, "extern/Streamline/tools/project-release.ps1"),
            "-Mode",
            "GeneratePublicHeader",
            "-OutputPath",
            trust_header
        })
    end)
end)
