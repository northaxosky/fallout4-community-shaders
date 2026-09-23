function main(target)
    import("core.project.config")
    import("core.project.project")
    import("utils.archive")

    local directory = path.absolute(path.join(config.builddir(), "packages"))
    local filename = target:name() .. "-" .. (target:version() or "0.0.0") .. ".zip"
    local packagefile = path.join(directory, filename)
    local identity = config.get("release_identity") or ""
    if identity ~= "" then
        local destination = path.join(directory, target:name() .. "-" .. identity .. ".zip")
        os.mv(packagefile, destination)
        packagefile = destination
    end

    local staged = path.join(os.tmpdir(), "packages", project.name() or "", target:name())
    local extracted = os.tmpfile() .. ".archive"
    try {
        function()
            archive.extract(packagefile, extracted)
            local count = 0
            for _, source in ipairs(os.files(path.join(staged, "**"))) do
                local relative = path.relative(source, staged)
                assert(not table.contains({ ".lib", ".exp", ".obj" }, path.extension(relative):lower()),
                    "build intermediate in package: " .. relative)
                local actual = path.join(extracted, relative)
                assert(os.isfile(actual), "missing archive payload: " .. relative)
                assert(hash.sha256(source) == hash.sha256(actual), "archive payload mismatch: " .. relative)
                count = count + 1
            end
            assert(count > 0 and #os.files(path.join(extracted, "**")) == count,
                "unexpected archive contents")
            print("Verified release archive: " .. packagefile)
        end,
        finally {
            function(ok, errors)
                os.tryrm(extracted)
                if not ok then raise(errors) end
            end
        }
    }
end
