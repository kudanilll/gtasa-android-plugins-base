# AML Mod Template with Plugin-SDK (GTA:SA Android)

A starter template for building Android Mod Loader (AML) mods for GTA San Andreas on Android, powered by an integrated and modernized **Plugin-SDK**.

## Features

- **Integrated Plugin-SDK**:
  - Focused strictly on GTA:SA (`plugin-sdk/game_sa/`).
  - RenderWare types (`plugin-sdk/renderware/`), GTA base math & types (`plugin-sdk/gta_base/`), and event hook system (`Events::*`).

## Project Structure

```text
base_plugins/
├── Android.mk                  # NDK build configuration
├── Application.mk              # ABI & STL configuration
├── build.ps1                   # PowerShell build script
├── NDKPath.txt                 # Path to Android NDK
├── main.cpp                    # Mod entry point & in-game HUD example
├── mod/                        # AML core library (amlmod.h, logger, config)
└── plugin-sdk/                 # Integrated GTA:SA Plugin-SDK
    ├── game_sa/                # GTA:SA specific classes, engine, events, entity
    ├── gta_base/               # Basic math & color types (Vector, Rect, RGBA, etc.)
    ├── renderware/             # RenderWare engine structures (RwTexture, RpAtomic, etc.)
    ├── sdk_base.h              # Base address & symbol resolution
    ├── sdk_basetypes.h         # Common types & macros
    └── sdk_callevents.h        # Hooking & event dispatch system
```

## Requirements

- Android NDK (r21 or newer)
- PowerShell (Windows PowerShell or PowerShell 7+)

## Setup & Build

1. Set your NDK root path in `NDKPath.txt`:

   ```text
   D:\Android\Sdk\ndk\27.1.12297006
   ```

2. Run the build script:

   ```powershell
   ./build.ps1
   ```

3. Built shared libraries (`libmodtemplate.so`) will output to:
   - `libs/armeabi-v7a/libmodtemplate.so`
   - `libs/arm64-v8a/libmodtemplate.so`
