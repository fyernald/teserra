#!/usr/bin/env bash
# Teserra machine setup (#210): checks everything a Windows machine needs to build Teserra, installs what is
# missing, and ends with a configured, built and tested project. Run it from Git Bash, anywhere in the clone:
#
#   bash tools/setup/setup.sh            check, install what is missing, configure, build and test
#   bash tools/setup/setup.sh --check    only report what is there and what is missing; change nothing
#   bash tools/setup/setup.sh --help     every option
#
# Git for Windows is the one prerequisite: it provides bash, curl and git. Installers are downloaded from their
# official sources and run only with a valid Authenticode signature of the expected publisher. Running the
# script again is safe: what is installed is skipped. Background: docs/guides/build-and-ci.md.

set -uo pipefail

# ---------------------------------------------------------------------------------------------------------
# What a machine needs

readonly PYTHON_VERSION="3.13.9"
readonly PYTHON_MINIMUM="3.10" # tools/scripts use 3.10 syntax
readonly PYTHON_URL="https://www.python.org/ftp/python/${PYTHON_VERSION}/python-${PYTHON_VERSION}-amd64.exe"
readonly PYTHON_PUBLISHER="Python Software Foundation"
# Visual Studio 2022 Build Tools: the compiler, CMake, Ninja, vcpkg and a Windows SDK, without the IDE. An
# existing Visual Studio 2022 17.8+ or 2026 with the C++ workload counts as well.
readonly BUILD_TOOLS_URL="https://aka.ms/vs/17/release/vs_BuildTools.exe"
readonly MICROSOFT_PUBLISHER="Microsoft Corporation"
readonly VS_MINIMUM_VERSION="17.8"
readonly VS_REQUIRED=(Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project)
readonly VS_INSTALL=(
    --add Microsoft.VisualStudio.Workload.VCTools
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64
    --add Microsoft.VisualStudio.Component.VC.CMake.Project
    --add Microsoft.VisualStudio.Component.Vcpkg
    --add Microsoft.VisualStudio.Component.Windows11SDK.26100
)
# clang-format: the version CI pins (.github/workflows/ci.yml); other versions may format differently.
# clang-tidy: the nearest 22.1 release on PyPI; CI does not run clang-tidy yet (#142).
readonly CLANG_FORMAT_VERSION="22.1.3"
readonly CLANG_TIDY_VERSION="22.1.8"
readonly MIN_FREE_GB=10
readonly PRESET="windows-msvc-debug"

# ---------------------------------------------------------------------------------------------------------
# Options

CHECK_ONLY=0
ASSUME_YES=0
WITH_LLVM=1
WITH_CONFIGURE=1
WITH_TESTS=1

usage() {
    cat <<'EOF'
Usage: bash tools/setup/setup.sh [options]

Checks what building Teserra needs, installs what is missing, then configures, builds and tests the project.

Options:
  --check            only report; install and build nothing (exit 1 if something is missing)
  --yes              install without asking first
  --skip-llvm        neither check nor install clang-format / clang-tidy
  --skip-configure   stop after the tools (no configure, build or tests)
  --skip-tests       configure and build, but do not run the tests
  --help             show this text
EOF
}

parse_options() {
    local argument
    for argument in "$@"; do
        case "$argument" in
            --check) CHECK_ONLY=1 ;;
            --yes) ASSUME_YES=1 ;;
            --skip-llvm) WITH_LLVM=0 ;;
            --skip-configure) WITH_CONFIGURE=0 ;;
            --skip-tests) WITH_TESTS=0 ;;
            --help | -h)
                usage
                exit 0
                ;;
            *)
                echo "Unknown option: $argument" >&2
                usage >&2
                exit 2
                ;;
        esac
    done
}

# ---------------------------------------------------------------------------------------------------------
# Output

if [[ -t 1 ]]; then
    readonly BOLD=$'\e[1m' GREEN=$'\e[32m' YELLOW=$'\e[33m' RED=$'\e[31m' DIM=$'\e[2m' RESET=$'\e[0m'
else
    readonly BOLD="" GREEN="" YELLOW="" RED="" DIM="" RESET=""
fi

PROBLEMS=()

section() { printf '\n%s== %s ==%s\n' "$BOLD" "$1" "$RESET"; }
ok() { printf '  %s[ok]%s      %s\n' "$GREEN" "$RESET" "$1"; }
missing() { printf '  %s[missing]%s %s\n' "$YELLOW" "$RESET" "$1"; }
note() { printf '  %s%s%s\n' "$DIM" "$1" "$RESET"; }

# Reports a problem now and again in the summary: what is wrong, then how to fix it.
problem() {
    printf '  %s[problem]%s %s\n' "$RED" "$RESET" "$1"
    printf '            %sFix:%s %s\n' "$BOLD" "$RESET" "$2"
    PROBLEMS+=("$1"$'\n'"      Fix: $2")
}

ask() {
    if ((ASSUME_YES)); then
        return 0
    fi
    local reply
    read -r -p "  $1 [Y/n] " reply
    [[ -z "$reply" || "$reply" =~ ^[Yy] ]]
}

# A path as Windows programs need it.
windows_path() { cygpath -w "$1"; }

# Runs a Windows program without MSYS rewriting its /switches into paths.
run_windows() { MSYS2_ARG_CONV_EXCL='*' "$@"; }

# ---------------------------------------------------------------------------------------------------------
# Downloads with a progress bar

human_mb() { awk -v bytes="$1" 'BEGIN { printf "%.1f MB", bytes / 1048576 }'; }

# Downloads `url` to `file`, showing what it downloads, a bar, the size, the speed and the time left.
download() {
    local url=$1 file=$2 label=$3
    local total
    total=$(curl -sIL --max-time 30 "$url" | tr -d '\r' | awk 'tolower($1) == "content-length:" { n = $2 } END { print n + 0 }')
    printf '  Downloading %s\n  %sfrom %s%s\n' "$label" "$DIM" "$url" "$RESET"
    rm -f "$file"
    curl -sSL --fail --retry 3 --connect-timeout 20 -o "$file" "$url" &
    local pid=$! start=$SECONDS now size previous=0 speed=0 width=30
    while kill -0 "$pid" 2>/dev/null; do
        sleep 0.5
        size=$(stat -c %s "$file" 2>/dev/null || echo 0)
        now=$SECONDS
        speed=$(((size - previous) * 2)) # bytes in the last half second, per second
        previous=$size
        if ((total > 0)); then
            local percent=$((size * 100 / total)) filled=$((size * width / total)) eta="?"
            ((speed > 0)) && eta="$(((total - size) / speed))s"
            printf '\r  [%-*s] %3d%%  %s / %s  %s/s  %s left   ' "$width" "$(printf '%*s' "$filled" '' | tr ' ' '#')" \
                "$percent" "$(human_mb "$size")" "$(human_mb "$total")" "$(human_mb "$speed")" "$eta"
        else
            printf '\r  %s  %s/s  (%ss)   ' "$(human_mb "$size")" "$(human_mb "$speed")" "$((now - start))"
        fi
    done
    wait "$pid"
    local status=$?
    printf '\n'
    if ((status != 0)); then
        problem "Downloading $label failed (curl exit code $status)." \
            "Check the internet connection. Behind a proxy, set HTTPS_PROXY=http://proxy:port and run again. Or download $url by hand and run it."
        return 1
    fi
    note "Downloaded $(human_mb "$(stat -c %s "$file")") in $((SECONDS - start)) s"
}

# True if `file` carries a valid Authenticode signature whose signer is `publisher`.
signed_by() {
    local file=$1 publisher=$2 result
    result=$(run_windows powershell.exe -NoProfile -NonInteractive -Command \
        "\$s = Get-AuthenticodeSignature -LiteralPath '$(windows_path "$file")'; \"\$(\$s.Status)|\$(\$s.SignerCertificate.Subject)\"" |
        tr -d '\r')
    if [[ "$result" == Valid\|*"CN=$publisher,"* || "$result" == Valid\|*"CN=$publisher" ]]; then
        note "Signature: valid, $publisher"
        return 0
    fi
    problem "The downloaded installer is not signed by $publisher (got: ${result:-no answer}); it was not run." \
        "Delete $(windows_path "$file") and run the script again. If it happens again, download the installer from the official site by hand; something may be tampering with downloads."
    return 1
}

# ---------------------------------------------------------------------------------------------------------
# Checks

REPO_ROOT=""
PYTHON=()          # how to run Python, e.g. (py -3)
VS_PATH=""         # Visual Studio with the C++ tools
NEED_PYTHON=0
NEED_BUILD_TOOLS=0
NEED_LLVM=0

check_platform() {
    case "$(uname -s)" in
        MINGW* | MSYS* | CYGWIN*) ok "Windows, $(uname -s) bash ${BASH_VERSION%%(*}" ;;
        *)
            problem "This script sets up Windows machines; this is $(uname -s)." \
                "Run it from Git Bash on Windows. Linux support comes with the Linux port (#127)."
            return 1
            ;;
    esac
    local script_dir
    script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    REPO_ROOT=$(cd "$script_dir/../.." && pwd)
    if [[ ! -f "$REPO_ROOT/CMakePresets.json" ]]; then
        problem "The script is not inside a Teserra clone ($REPO_ROOT has no CMakePresets.json)." \
            "Clone the repository with 'git clone https://github.com/fyernald/teserra.git' and run tools/setup/setup.sh from it."
        return 1
    fi
    ok "Repository at $(windows_path "$REPO_ROOT")"
}

check_disk() {
    local free_kb free_gb
    free_kb=$(df -Pk "$REPO_ROOT" | awk 'NR == 2 { print $4 }')
    free_gb=$((free_kb / 1048576))
    if ((free_gb >= MIN_FREE_GB)); then
        ok "Free disk space: $free_gb GB"
    else
        problem "Only $free_gb GB free on the repository's drive; the tools and the first build need about $MIN_FREE_GB GB." \
            "Free some space (Settings > System > Storage), or clone the repository to a drive with more room."
    fi
}

check_internet() {
    if ! command -v curl >/dev/null; then
        problem "curl is not available, so nothing can be downloaded."             "curl comes with Git for Windows: run the script from Git Bash, not from another shell."
        return
    fi
    if curl -sI --max-time 15 https://github.com >/dev/null; then
        ok "Internet access"
    else
        problem "github.com cannot be reached, so nothing can be downloaded." \
            "Check the connection. Behind a proxy, set HTTPS_PROXY=http://proxy:port before running the script."
    fi
}

check_git() {
    if command -v git >/dev/null; then
        ok "$(git --version)"
    else
        problem "git is not on PATH." "Install Git for Windows from https://git-scm.com and run the script from Git Bash."
    fi
}

# Sets PYTHON to a Python >= PYTHON_MINIMUM, or marks it missing.
check_python() {
    local candidate version
    local -a candidates=("py -3" "python" "python3") command
    # Where the installer below puts it, before a new terminal has it on PATH; a path may contain spaces.
    local default_install="${LOCALAPPDATA:-}/Programs/Python/Python313/python.exe"
    [[ -n "${LOCALAPPDATA:-}" ]] && candidates+=("$(cygpath -u "$default_install")")
    for candidate in "${candidates[@]}"; do
        if [[ "$candidate" == /* ]]; then
            command=("$candidate")
        else
            read -r -a command <<<"$candidate"
        fi
        version=$("${command[@]}" -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])' 2>/dev/null | tr -d '\r')
        [[ -z "$version" ]] && continue
        if [[ "$(printf '%s\n%s\n' "$PYTHON_MINIMUM" "${version%.*}" | sort -V | head -1)" == "$PYTHON_MINIMUM" ]]; then
            PYTHON=("${command[@]}")
            ok "Python $version ($candidate)"
            return 0
        fi
        note "Python $version ($candidate) is older than $PYTHON_MINIMUM"
    done
    # The Microsoft Store puts a 'python' on PATH that only opens the Store.
    if command -v python 2>/dev/null | grep -qi WindowsApps; then
        note "The 'python' on PATH is the Microsoft Store shortcut, not an installed Python."
    fi
    missing "Python $PYTHON_MINIMUM or newer"
    NEED_PYTHON=1
}

check_visual_studio() {
    # TESERRA_SETUP_VSWHERE points the check elsewhere, so tests can see a machine without Visual Studio.
    local vswhere="${TESERRA_SETUP_VSWHERE:-/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe}"
    if [[ ! -x "$vswhere" ]]; then
        missing "Visual Studio Build Tools (no Visual Studio installed)"
        NEED_BUILD_TOOLS=1
        return
    fi
    VS_PATH=$(run_windows "$vswhere" -latest -products '*' -version "[$VS_MINIMUM_VERSION," \
        -requires "${VS_REQUIRED[@]}" -property installationPath | tr -d '\r')
    if [[ -z "$VS_PATH" ]]; then
        local any
        any=$(run_windows "$vswhere" -latest -products '*' -property displayName | tr -d '\r')
        if [[ -n "$any" ]]; then
            note "$any is installed, but without the C++ tools and CMake, or older than $VS_MINIMUM_VERSION."
            note "Installing the Build Tools adds them beside it; or open Visual Studio Installer > Modify > 'Desktop development with C++'."
        fi
        missing "Visual Studio Build Tools with the C++ workload"
        NEED_BUILD_TOOLS=1
        return
    fi
    ok "$(run_windows "$vswhere" -path "$VS_PATH" -property displayName | tr -d '\r') $(run_windows "$vswhere" -path "$VS_PATH" -property catalog_productDisplayVersion | tr -d '\r'): C++ tools, CMake, Ninja"

    # vcpkg: Visual Studio's own, or a standalone clone that VCPKG_ROOT names.
    if [[ -n "$(run_windows "$vswhere" -path "$VS_PATH" -requires Microsoft.VisualStudio.Component.Vcpkg -property installationPath | tr -d '\r')" ]]; then
        ok "vcpkg (Visual Studio component)"
    elif [[ -n "${VCPKG_ROOT:-}" && -f "$(cygpath -u "$VCPKG_ROOT")/vcpkg.exe" ]]; then
        ok "vcpkg at VCPKG_ROOT=$VCPKG_ROOT"
    else
        missing "vcpkg (the Visual Studio component, or a clone that VCPKG_ROOT names)"
        NEED_BUILD_TOOLS=1
    fi

    if compgen -G "/c/Program Files (x86)/Windows Kits/10/Include/10.*/um/windows.h" >/dev/null; then
        ok "Windows SDK"
    else
        missing "Windows SDK"
        NEED_BUILD_TOOLS=1
    fi
}

# The version that `tool --version` prints, e.g. 22.1.3; empty if it does not run.
tool_version() { "$@" --version 2>/dev/null | tr -d '\r' | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1; }

check_llvm() {
    local -a places=()
    [[ -n "$VS_PATH" ]] && places+=("$(cygpath -u "$VS_PATH")/VC/Tools/Llvm/x64/bin")
    if ((${#PYTHON[@]})); then
        local scripts
        scripts=$("${PYTHON[@]}" -c 'import sysconfig; print(sysconfig.get_path("scripts", "nt_user"))' 2>/dev/null | tr -d '\r')
        [[ -n "$scripts" ]] && places+=("$(cygpath -u "$scripts")")
    fi
    local format_ok=0 tidy_ok=0 place version
    for place in "" "${places[@]}"; do
        local prefix=${place:+$place/}
        if ((!format_ok)); then
            version=$(tool_version "${prefix}clang-format")
            [[ "$version" == "$CLANG_FORMAT_VERSION" ]] && format_ok=1 && ok "clang-format $version${place:+ ($(windows_path "$place"))}"
        fi
        if ((!tidy_ok)); then
            version=$(tool_version "${prefix}clang-tidy")
            [[ "$version" == 22.* ]] && tidy_ok=1 && ok "clang-tidy $version${place:+ ($(windows_path "$place"))}"
        fi
    done
    ((format_ok)) || missing "clang-format $CLANG_FORMAT_VERSION (the version CI pins)"
    ((tidy_ok)) || missing "clang-tidy 22.1"
    if ((!format_ok || !tidy_ok)); then
        NEED_LLVM=1
    fi
}

run_checks() {
    section "System"
    check_platform || return 1
    check_disk
    check_internet
    check_git
    section "Python"
    check_python
    section "Compiler, CMake, Ninja, vcpkg, Windows SDK"
    check_visual_studio
    if ((WITH_LLVM)); then
        section "LLVM tools"
        check_llvm
    fi
}

# ---------------------------------------------------------------------------------------------------------
# Installs

DOWNLOADS=""

install_python() {
    section "Installing Python $PYTHON_VERSION"
    ask "Download and install Python $PYTHON_VERSION for this user from python.org?" || {
        problem "Python was not installed." "Install Python $PYTHON_MINIMUM+ from https://www.python.org/downloads/ and run the script again."
        return 1
    }
    local installer="$DOWNLOADS/python-$PYTHON_VERSION-amd64.exe"
    download "$PYTHON_URL" "$installer" "Python $PYTHON_VERSION" || return 1
    signed_by "$installer" "$PYTHON_PUBLISHER" || return 1
    note "Installing quietly for this user (no admin rights needed)..."
    run_windows "$installer" /quiet InstallAllUsers=0 PrependPath=1 Include_launcher=1 Include_test=0
    local status=$?
    if ((status != 0)); then
        problem "The Python installer failed (exit code $status)." \
            "Run $(windows_path "$installer") by hand to see its message; its log is in %TEMP%\\Python*.log."
        return 1
    fi
    ok "Python $PYTHON_VERSION installed. New terminals find it on PATH."
}

install_build_tools() {
    section "Installing Visual Studio Build Tools"
    note "About 3-6 GB: the compiler, CMake, Ninja, vcpkg and the Windows SDK, without the IDE."
    ask "Download and install the Visual Studio 2022 Build Tools from Microsoft?" || {
        problem "The Build Tools were not installed." \
            "Install Visual Studio 2022 or 2026 with 'Desktop development with C++' and run the script again."
        return 1
    }
    local installer="$DOWNLOADS/vs_BuildTools.exe"
    download "$BUILD_TOOLS_URL" "$installer" "the Visual Studio Build Tools installer" || return 1
    signed_by "$installer" "$MICROSOFT_PUBLISHER" || return 1
    note "The Visual Studio installer now shows its own progress window while it downloads and installs."
    note "Windows asks for administrator rights: allow it."
    run_windows "$installer" --passive --wait --norestart "${VS_INSTALL[@]}"
    local status=$?
    case $status in
        0) ok "Build Tools installed" ;;
        3010) ok "Build Tools installed; Windows needs a restart before the first build" ;;
        1602)
            problem "The Build Tools installation was cancelled." "Run the script again and let the installer finish."
            return 1
            ;;
        5007 | 1)
            problem "The Build Tools installer was not allowed to run (exit code $status)." \
                "Allow the administrator prompt, or run Git Bash as administrator, then run the script again."
            return 1
            ;;
        *)
            problem "The Build Tools installer failed (exit code $status)." \
                "Its logs are in %TEMP%\\dd_*.log. A pending Windows update or restart often causes this: restart and run the script again."
            return 1
            ;;
    esac
}

install_llvm() {
    section "Installing clang-format $CLANG_FORMAT_VERSION and clang-tidy $CLANG_TIDY_VERSION"
    if ((!${#PYTHON[@]})); then
        problem "The LLVM tools are installed with pip, and there is no Python." "Install Python first (run the script again)."
        return 1
    fi
    ask "Install them for this user with pip?" || return 1
    "${PYTHON[@]}" -m pip install --user --disable-pip-version-check \
        "clang-format==$CLANG_FORMAT_VERSION" "clang-tidy==$CLANG_TIDY_VERSION" || {
        problem "pip could not install the LLVM tools." \
            "Check the internet connection (or HTTPS_PROXY), then run: ${PYTHON[*]} -m pip install --user clang-format==$CLANG_FORMAT_VERSION clang-tidy==$CLANG_TIDY_VERSION"
        return 1
    }
    ok "LLVM tools installed in Python's user scripts directory"
}

install_missing() {
    DOWNLOADS=$(mktemp -d "${TMPDIR:-/tmp}/teserra-setup.XXXXXX")
    ((NEED_PYTHON)) && install_python
    ((NEED_BUILD_TOOLS)) && install_build_tools
    if ((NEED_PYTHON)); then # the new Python is not on this shell's PATH yet
        PYTHON=()
        check_python >/dev/null
    fi
    if ((WITH_LLVM && NEED_LLVM)); then
        install_llvm
    fi
    rm -rf "$DOWNLOADS"
}

# ---------------------------------------------------------------------------------------------------------
# Configure, build, test

# Runs `command` in the Visual Studio developer environment (vcvars64.bat), from the repository root.
in_developer_environment() {
    local script
    script=$(mktemp "${TMPDIR:-/tmp}/teserra-setup.XXXXXX.cmd")
    printf '@echo off\r\ncall "%s\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 90\r\ncd /d "%s" || exit /b 91\r\n%s\r\n' \
        "$VS_PATH" "$(windows_path "$REPO_ROOT")" "$1" >"$script"
    run_windows cmd.exe /d /c "$(windows_path "$script")"
    local status=$?
    rm -f "$script"
    return $status
}

build_project() {
    section "Configure (vcpkg downloads and builds the libraries the first time: several minutes)"
    if ! in_developer_environment "cmake --preset $PRESET"; then
        problem "CMake could not configure the project." \
            "Read the error above. vcpkg failures are logged in build\\$PRESET\\vcpkg-manifest-install.log; a message about VCPKG_ROOT means the vcpkg component is missing (run the script again)."
        return 1
    fi
    section "Build"
    if ! in_developer_environment "cmake --build --preset $PRESET"; then
        problem "The build failed." "Read the first error above. If the checkout's path is very long, clone closer to the drive root (C:\\src\\teserra)."
        return 1
    fi
    ok "Built in build\\$PRESET"
    ((WITH_TESTS)) || return 0
    section "Tests"
    if ! in_developer_environment "ctest --preset $PRESET"; then
        problem "Some tests failed." "Run 'ctest --preset $PRESET --output-on-failure' in a Developer PowerShell to see why, and report it with the log."
        return 1
    fi
    ok "All tests passed"
}

# ---------------------------------------------------------------------------------------------------------

summary() {
    section "Summary"
    if ((${#PROBLEMS[@]} == 0)); then
        if ((CHECK_ONLY)); then
            ok "Everything needed is installed."
        else
            ok "The machine is ready. Build with 'cmake --build --preset $PRESET' in a Developer PowerShell for VS."
        fi
        return 0
    fi
    printf '  %s%d problem(s):%s\n' "$RED" "${#PROBLEMS[@]}" "$RESET"
    local entry
    for entry in "${PROBLEMS[@]}"; do
        printf '  - %s\n' "$entry"
    done
    return 1
}

main() {
    printf '%sTeserra setup%s%s\n' "$BOLD" "$RESET" "$( ((CHECK_ONLY)) && echo ' (check only)')"
    run_checks
    local needs=$((NEED_PYTHON + NEED_BUILD_TOOLS + NEED_LLVM))
    if ((CHECK_ONLY)); then
        if ((needs > 0)); then
            problem "$needs part(s) are missing (marked [missing] above)." "Run 'bash tools/setup/setup.sh' without --check to install them."
        fi
        summary
        return
    fi
    if ((${#PROBLEMS[@]} > 0)); then # the machine cannot install anything until these are fixed
        summary
        return 1
    fi
    if ((needs > 0)); then
        install_missing
        section "Checking again"
        NEED_PYTHON=0 NEED_BUILD_TOOLS=0 NEED_LLVM=0
        run_checks
        needs=$((NEED_PYTHON + NEED_BUILD_TOOLS + NEED_LLVM))
        ((needs == 0)) || problem "$needs part(s) are still missing after installing." "Read the problems above; a restart may be needed after the Build Tools."
    fi
    if ((WITH_CONFIGURE && needs == 0 && ${#PROBLEMS[@]} == 0)); then
        build_project
    fi
    summary
}

# Sourcing the script (tests) defines the functions without running it.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    parse_options "$@"
    main
fi
