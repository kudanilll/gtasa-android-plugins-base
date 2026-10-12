# AML Mod Template (GTA:SA Android)

A starter template for building Android Mod Loader (AML) mods for GTA San Andreas on Android.

## Requirements

- Android NDK (r21 or newer)
- PowerShell

## Setup & Build

1. Put your NDK root path in `NDKPath.txt`:

   ```text
   D:\android-ndk
   ```

2. Run the build script:

   ```powershell
   ./build.ps1
   ```

Built binaries will output to `libs/armeabi-v7a/` and `libs/arm64-v8a/`.
