function main()
    import("core.base.option")
    import("core.base.semver")
    import("core.project.project")

    local function git(...)
        return os.iorunv("git", table.join({ "-C", os.projectdir() }, { ... })):trim()
    end
    local function version_parts(version)
        local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)$")
        assert(major and semver.is_valid(version), "plugin_version must be X.Y.Z semver")
        return major, minor, patch
    end

    local version = project.version()
    local major, minor, patch = version_parts(version)
    local stable = option.get("dispatch") or false
    local before = option.get("before")
    if before and before ~= "" and not before:match("^0+$") then
        assert(before:match("^%x+$"), "before must be a commit SHA")
        local reachable = try { function() return git("rev-parse", "--verify", before .. "^{commit}") end }
        if reachable then
            local text = git("show", before .. ":xmake.lua")
            local previous = assert(text:match('local%s+plugin_version%s*=%s*"([^"]+)"'),
                "previous revision has no plugin_version declaration")
            version_parts(previous)
            if version ~= previous then
                assert(semver.compare(version, previous) > 0, "release version must increase")
                stable = true
            end
        end
    end

    local last_stable
    for tag in git("tag", "--merged", "HEAD", "--list", "v*"):gmatch("[^\r\n]+") do
        local candidate = tag:match("^v(%d+%.%d+%.%d+)$")
        if candidate and semver.is_valid(candidate) and
            (not last_stable or semver.compare(candidate, last_stable:sub(2)) > 0) then
            last_stable = tag
        end
    end

    local tag = "v" .. version
    if not stable then
        local base = version
        if git("tag", "--list", tag) ~= "" then
            base = major .. "." .. minor .. "." .. tostring(tonumber(patch) + 1)
        end
        local count = git("rev-list", "--count", last_stable and (last_stable .. "..HEAD") or "HEAD")
        tag = "v" .. base .. "-dev." .. count
    end

    local output = table.concat({
        "channel=" .. (stable and "stable" or "dev"),
        "tag=" .. tag,
        "version=" .. version,
        "identity=" .. tag,
        "previous-stable-tag=" .. (last_stable or ""),
        "publish=" .. tostring(not option.get("no-publish"))
    }, "\n") .. "\n"
    if option.get("output") then
        local file = assert(io.open(option.get("output"), "a"))
        file:write(output)
        file:close()
    else
        io.write(output)
    end
end
