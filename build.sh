#!/usr/bin/env bash
# Linux/macOS counterpart of build.ps1.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -f NDKPath.txt ]; then
    echo "NDKPath.txt not found. Put your Android NDK path in it." >&2
    exit 1
fi
NDK_PATH=
read -r NDK_PATH <NDKPath.txt || true # true: the file may lack a final newline
NDK_PATH=${NDK_PATH%$'\r'}
echo "NDK located at: $NDK_PATH"

NDK_BUILD="$NDK_PATH/ndk-build"
[ -f "$NDK_BUILD" ] || NDK_BUILD="$NDK_PATH/build/ndk-build"

echo "[BUILD] Starting NDK from $PWD..."
# Relative paths keep make happy when the project path contains spaces.
"$NDK_BUILD" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk NDK_DEBUG=0
echo "[BUILD] Done!"
