# AML mod template for GTA SA Android

A template for building GTA San Andreas Android plugins with Android Mod Loader (AML).

## Features

- Integrated GTA SA Plugin-SDK and RenderWare support.
- Dear ImGui 1.93.1 WIP with a RenderWare renderer and touch input.
- External Image loading through `stb_image`.
- ARM32 (`armeabi-v7a`) and ARM64 (`arm64-v8a`) build targets for GTA SA v2.00/v2.10.
- Build and formatting scripts, plus a GitHub Actions workflow.

## Build

Set the Android NDK path in `NDKPath.txt`, then run:

```powershell
.\build.ps1
```

The compiled libraries are saved to:

- `libs/armeabi-v7a/libmodtemplate.so`
- `libs/arm64-v8a/libmodtemplate.so`

Copy the `.so` for your game's ABI to the AML mods folder.

## Credits

- [AML](https://github.com/AndroidModLoader/AndroidModLoader) and [aml-psdk](https://github.com/AndroidModLoader/aml-psdk): RusJJ and contributors.
- [Dear ImGui](https://github.com/ocornut/imgui): Omar Cornut and contributors. [MIT](mod/thirdparty/imgui/LICENSE.txt).
- [stb](https://github.com/nothings/stb): Sean Barrett and contributors. [License](mod/thirdparty/stb/STB_LICENSE).
- RenderWare integration references: [SA-MP Mobile v2.10](https://github.com/noobcooder-sys/SA-MP-Mobile-Client-GTA-SA-2.10-) and [build69-gtasa2.0](https://github.com/0xcds4r/build69-gtasa2.0).
- Base template: [MIT](LICENSE). Plugin-SDK: [GPLv3](plugin-sdk/LICENSE).
