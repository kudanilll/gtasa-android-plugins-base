#!/usr/bin/env bash
# Creates a new mod from this template:
#   curl -fsSL https://raw.githubusercontent.com/kudanilll/gtasa-android-plugins-base/master/create.sh | bash
# Keep the steps in sync with create.ps1. Written for bash 3.2 (macOS) and BSD/GNU tools alike.
set -euo pipefail

REPO=kudanilll/gtasa-android-plugins-base
BRANCH=master
COMMIT_MESSAGE="Initialise project from the AML mod template by 0xavion"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    CYAN=$'\033[36m' MAGENTA=$'\033[35m' GRAY=$'\033[90m' RED=$'\033[31m' GREEN=$'\033[32m' YELLOW=$'\033[33m'
    LIGHT_GREEN=$'\033[92m' BLUE=$'\033[94m' LIGHT_RED=$'\033[91m' WHITE=$'\033[97m' BOLD=$'\033[1m' RESET=$'\033[0m'
else
    CYAN= MAGENTA= GRAY= RED= GREEN= YELLOW= LIGHT_GREEN= BLUE= LIGHT_RED= WHITE= BOLD= RESET=
fi

# "AML for GTASA ANDROID": A light green, M blue, L light red, "for" gray, the rest white.
banner() {
    printf '%s▄▀█ %s█▀▄▀█ %s█       %s█▀▀ ▀█▀ ▄▀█ █▀ ▄▀█   ▄▀█ █▄ █ █▀▄ █▀█ █▀█ █ █▀▄%s\n' \
        "$LIGHT_GREEN" "$BLUE" "$LIGHT_RED" "$WHITE" "$RESET"
    printf '%s█▀█ %s█ ▀ █ %s█▄▄ %sfor %s█▄█  █  █▀█ ▄█ █▀█   █▀█ █ ▀█ █▄▀ █▀▄ █▄█ █ █▄▀%s\n' \
        "$LIGHT_GREEN" "$BLUE" "$LIGHT_RED" "$GRAY" "$WHITE" "$RESET"
    printf '%sBy 0xavion%s\n\n' "$GRAY" "$RESET"
}

trim() {
    local s=$1
    s=${s#"${s%%[![:space:]]*}"}
    printf '%s' "${s%"${s##*[![:space:]]}"}"
}

slug() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr -cd 'a-z0-9_'; }

# Validators print the problem and fail, or stay quiet and succeed.
valid_text() {
    local re='^[A-Za-z0-9][A-Za-z0-9 ._-]*$'
    [[ $1 =~ $re ]] || { echo "Use letters, digits, spaces, dots, dashes or underscores"; return 1; }
}
valid_version() {
    local re='^[A-Za-z0-9._-]+$'
    [[ $1 =~ $re ]] || { echo "Use letters, digits, dots, dashes or underscores (no spaces)"; return 1; }
}
valid_id() {
    local re='^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)*$'
    [[ $1 =~ $re ]] || { echo "Use letters, digits and underscores, separated by dots"; return 1; }
}
valid_lib() {
    local re='^[A-Za-z0-9_]+$'
    [[ $1 =~ $re ]] || { echo "Use letters, digits or underscores"; return 1; }
}
valid_folder() {
    local re='^[A-Za-z0-9_][A-Za-z0-9._-]*$'
    [[ $1 =~ $re ]] || { echo "Use letters, digits, dots, dashes or underscores"; return 1; }
    if [ -e "$1" ] && [ -n "$(ls -A "$1" 2>/dev/null)" ]; then
        echo "$1 already exists and is not empty"
        return 1
    fi
}
is_ndk() { [ -n "$1" ] && { [ -f "$1/ndk-build" ] || [ -f "$1/build/ndk-build" ]; }; }
valid_ndk() { [ -z "$1" ] || is_ndk "$1" || { echo "No ndk-build in that folder"; return 1; }; }

# ask LABEL DEFAULT VALIDATOR: asks until the answer passes, then prints it. Prompts go to stderr.
ask() {
    local label=$1 default=$2 validate=$3 reply error
    while :; do
        printf '%s?%s %s ' "$MAGENTA" "$RESET" "$label" >&2
        [ -n "$default" ] && printf '%s(%s)%s ' "$GRAY" "$default" "$RESET" >&2
        printf '» ' >&2
        IFS= read -r reply <&3 || { echo >&2; exit 1; }
        reply=$(trim "$reply")
        [ -n "$reply" ] || reply=$default
        if [ -z "$reply" ] && [ "$validate" != valid_ndk ]; then
            error="This field is required"
        elif error=$("$validate" "$reply"); then
            printf '%s' "$reply"
            return
        fi
        printf '  %s✗ %s%s\n' "$RED" "$error" "$RESET" >&2
    done
}

step() { printf '%s●%s %s... ' "$CYAN" "$RESET" "$1"; }
ok() { printf '%s%s%s\n' "$GREEN" "${1:-done}" "$RESET"; }
warn() { printf '  %s! %s%s\n' "$YELLOW" "$1" "$RESET"; }

# replace FILE FROM TO: literal, every occurrence.
replace() {
    local content
    content=$(cat "$1"; printf x)
    content=${content%x}
    case $content in *"$2"*) ;; *) warn "'$2' not found in $1"; return ;; esac
    printf '%s' "${content//"$2"/$3}" >"$1"
}

# replace_line FILE PREFIX NEW_LINE: every line starting with PREFIX becomes NEW_LINE.
replace_line() {
    local line found=
    while IFS= read -r line || [ -n "$line" ]; do
        case $line in "$2"*) line=$3 found=1 ;; esac
        printf '%s\n' "$line"
    done <"$1" >"$1.tmp"
    mv "$1.tmp" "$1"
    [ -n "$found" ] || warn "no line starting with '$2' in $1"
}

find_ndk() {
    local dir sdk version
    for dir in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}" "${ANDROID_NDK:-}"; do
        is_ndk "$dir" && { printf '%s' "$dir"; return; }
    done
    for sdk in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
        [ -n "$sdk" ] || continue
        # Newest first. Numeric keys instead of sort -V, which older macOS lacks.
        for version in $(ls -1 "$sdk/ndk" 2>/dev/null | sort -t. -k1,1nr -k2,2nr -k3,3nr); do
            is_ndk "$sdk/ndk/$version" && { printf '%s' "$sdk/ndk/$version"; return; }
        done
        is_ndk "$sdk/ndk-bundle" && { printf '%s' "$sdk/ndk-bundle"; return; }
    done
    return 1
}

main() {
    # Under `curl | bash` stdin is the script itself, so answers come from the terminal.
    if [ ! -t 0 ] && (exec </dev/tty) 2>/dev/null; then exec 3</dev/tty; else exec 3<&0; fi

    banner
    local name author version id lib folder pascal default_id ndk
    name=$(ask "Mod name" "" valid_text)
    author=$(ask "Author" "" valid_text)
    version=$(ask "Version" "1.0" valid_version)
    lib=$(slug "$name")
    [ -n "$lib" ] || lib=mymod
    default_id=$(slug "$author")
    default_id=${default_id:+$default_id.}$lib
    id=$(ask "Mod ID" "$default_id" valid_id)
    lib=$(ask "Library name" "$lib" valid_lib)
    folder=$(ask "Folder" "$lib" valid_folder)
    pascal=${name// /}
    echo

    step "Downloading template"
    local existed=
    [ -e "$folder" ] && existed=1
    mkdir -p "$folder"
    if ! curl -fsSL "https://codeload.github.com/$REPO/tar.gz/refs/heads/$BRANCH" |
        tar -xz -C "$folder" --strip-components=1; then
        printf '%sfailed%s\n' "$RED" "$RESET"
        [ -n "$existed" ] || rm -rf "$folder" # only what this run created
        exit 1
    fi
    rm -f "$folder/create.sh" "$folder/create.ps1"
    ok

    step "Applying names"
    (
        cd "$folder"
        replace_line Android.mk "LOCAL_MODULE " "LOCAL_MODULE    := $lib"
        replace_line main.cpp "MYMOD(" "MYMOD($id, $pascal, $version, $author)"
        replace main.cpp "Mod Template" "$name"
        replace .github/workflows/release.yml libmodtemplate "lib$lib"
        replace .github/workflows/release.yml BasePlugin-libs "$pascal-libs"
        replace README.md libyourmod "lib$lib"
        # New title; drop the template-only "Create a new mod" section.
        awk -v title="# $name" 'NR == 1 { print title; next } /^## / { skip = ($0 == "## Create a new mod") } !skip' \
            README.md >README.md.tmp
        mv README.md.tmp README.md
    )
    ok

    if ndk=$(find_ndk); then
        step "NDK found"
        ok "$ndk"
    else
        warn "Android NDK not found"
        ndk=$(ask "Android NDK path (empty to skip)" "" valid_ndk)
    fi
    if [ -n "$ndk" ]; then
        printf '%s\n' "$ndk" >"$folder/NDKPath.txt"
    else
        warn "Skipped. Put your NDK path in $folder/NDKPath.txt before building"
    fi

    if command -v git >/dev/null 2>&1; then
        step "git init + first commit"
        if ! git -C "$folder" init -q >/dev/null 2>&1; then
            ok "skipped (git init failed)"
        elif { git -C "$folder" add -A && git -C "$folder" commit -q -m "$COMMIT_MESSAGE"; } >/dev/null 2>&1; then
            ok
        else
            printf '%sinitialised, commit skipped (set git user.name and user.email)%s\n' "$YELLOW" "$RESET"
        fi
    fi

    printf '\n%s✔ Created %s%s\n\n' "$GREEN$BOLD" "$folder" "$RESET"
    printf '  Next steps:\n    cd %s\n    ./build.sh\n\n' "$folder"
}

main "$@"
