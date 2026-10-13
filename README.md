# AML Mod Template with Plugin-SDK (GTA:SA Android)

A starter template for building Android Mod Loader (AML) mods for GTA San Andreas on Android

The Plugin-SDK integrated into this project is sourced and adapted from [AndroidModLoader/aml-psdk](https://github.com/AndroidModLoader/aml-psdk) by RusJJ

## Features

- **Integrated Plugin-SDK**:
  - Sourced from [AndroidModLoader/aml-psdk](https://github.com/AndroidModLoader/aml-psdk)
  - Focused strictly on GTA:SA (`plugin-sdk/game_sa/`)
  - Clean absolute include paths (all relative `#include "../..."` and quoted paths normalized to `<plugin-sdk/...>`)
  - RenderWare types (`plugin-sdk/renderware/`), GTA base math & types (`plugin-sdk/gta_base/`), and event hook system (`Events::*`)
- Supports **GTA: SA v2.00** and **GTA: SA v2.10 (64-bit)**

## Project Structure

```text
base_plugins/
├── .clang-format               # LLVM-based code formatting configuration
├── format.ps1 / format.cmd     # Script to auto-format entire codebase
├── Android.mk                  # NDK build configuration
├── Application.mk              # ABI & STL configuration
├── build.ps1                   # PowerShell build script
├── NDKPath.txt                 # Path to Android NDK
├── main.cpp                    # Mod entry point & in-game HUD example
├── mod/                        # AML core library (amlmod.h, logger, config)
└── plugin-sdk/                 # Integrated GTA:SA Plugin-SDK (from aml-psdk)
    ├── LICENSE                 # GNU General Public License v3.0 (GPLv3)
    ├── game_sa/                # GTA:SA specific classes, engine, events, entity
    ├── gta_base/               # Basic math & color types (Vector, Rect, RGBA, etc.)
    ├── renderware/             # RenderWare engine structures (RwTexture, RpAtomic, etc.)
    ├── sdk_base.h              # Base address & symbol resolution
    ├── sdk_basetypes.h         # Common types & macros
    └── sdk_callevents.h        # Hooking & event dispatch system
```

## Requirements

- Android NDK (r21 or newer)
- C++17 or newer
- PowerShell (Windows PowerShell or PowerShell 7+)

## Setup & Build

1. Set your NDK root path in `NDKPath.txt`:

   ```text
   D:\Android\Sdk\ndk\27.1.12297006
   ```

2. Format code (optional):

   ```powershell
   ./format.ps1
   # atau double-click format.cmd
   ```

3. Run the build script:

   ```powershell
   ./build.ps1
   ```

4. Built shared libraries (`libmodtemplate.so`) will output to:
   - `libs/armeabi-v7a/libmodtemplate.so`
   - `libs/arm64-v8a/libmodtemplate.so`

## Credits & Attribution

- **[AML Plugin SDK (aml-psdk)](https://github.com/AndroidModLoader/aml-psdk)** - Created by **RusJJ** and contributors.
- **[Android Mod Loader (AML)](https://github.com/AndroidModLoader/AndroidModLoader)** - Android Mod Loader core and mod template.
- **[DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk)** - Structure and naming inspiration for GTA plugin SDK.
- **[stb_sprintf](https://github.com/nothings/stb)** - By Sean Barrett (stb).

## License

This project utilizes code under multiple open-source licenses:

- **Mod Template & Base Code**: Licensed under the [MIT License](LICENSE) (Copyright © 2026 RusJJ).
- **plugin-sdk/**: Sourced from [AndroidModLoader/aml-psdk](https://github.com/AndroidModLoader/aml-psdk) and licensed under the **[GNU General Public License v3.0 (GPLv3)](plugin-sdk/LICENSE)**.
- **mod/thirdparty/stb_sprintf.h**: Dual-licensed under Public Domain and the MIT License.
