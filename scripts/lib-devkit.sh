#!/bin/bash
# Shared helper for the devkit shims (deploy.sh, test.sh). Sourced, not executed.
# Resolves the sibling devkit workbench + a Windows PowerShell host, translating paths
# for WSL / MSYS so the Windows PowerShell can read -File, then runs a devkit verb.
#
#   devkit_run <project_root> <verb> [args...]
#
# Override the devkit location with DEVKIT_DIR.

devkit_run() {
    local project_root="$1"; shift
    local devkit="${DEVKIT_DIR:-$project_root/../devkit}"
    if [ ! -f "$devkit/devkit.ps1" ]; then
        echo "ERROR: devkit not found at $devkit - clone it as a sibling of the repo, or set DEVKIT_DIR" >&2
        return 1
    fi

    # The Windows PowerShell host can't read POSIX paths (/mnt/c/... under WSL, /c/...
    # under MSYS). Translate the script path to a Windows path when a translator exists.
    local devkit_ps1="$devkit/devkit.ps1"
    if command -v wslpath >/dev/null 2>&1; then
        devkit_ps1="$(wslpath -w "$devkit_ps1")"
    elif command -v cygpath >/dev/null 2>&1; then
        devkit_ps1="$(cygpath -w "$devkit_ps1")"
    fi

    # Prefer PowerShell 7 (pwsh); fall back to Windows PowerShell, including its stable
    # absolute path for the case where only a minimal (non-login) PATH is present.
    local ps=""
    local cand
    for cand in pwsh pwsh.exe powershell.exe powershell \
        "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" \
        "/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"; do
        if command -v "$cand" >/dev/null 2>&1 || [ -x "$cand" ]; then ps="$cand"; break; fi
    done
    if [ -z "$ps" ]; then
        echo "ERROR: PowerShell not found - need 'pwsh' or 'powershell.exe' on PATH" >&2
        return 1
    fi

    exec "$ps" -NoProfile -File "$devkit_ps1" "$@"
}
