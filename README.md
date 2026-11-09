# AML mod template for GTA SA Android

A template for building GTA San Andreas Android plugins with Android Mod Loader (AML).

## Features

- GTA SA Plugin-SDK with RenderWare support.
- Dear ImGui 1.93.1 WIP with a RenderWare renderer and touch input. The backend has no menu of its own; your render callback decides what to draw and when.
- Touches inside ImGui windows go to ImGui and everything else goes to the game, so the joystick and camera keep working while a menu is open.
- Several plugins built from this template can show ImGui at the same time. The window you touch comes to the front.
- Custom TTF/OTF fonts through ImGui's font API. Glyphs are rasterized on demand, so one font file covers every size.
- Image loading through `stb_image`.
- `MusicPlayer` plays MP3 and WAV files with Android's MediaPlayer and pauses while the game is in the background.
- ARM32 (`armeabi-v7a`) and ARM64 (`arm64-v8a`) build targets for GTA SA v2.00/v2.10.
- Build and formatting scripts, plus a GitHub Actions workflow.

## Example mod

`main.cpp` registers the mod and passes `Gui::Draw` to `ImGui_ImplRenderWare_Init`. `src/gui.cpp` draws a launcher button, a trainer window with time, weather, teleport and music tabs, and a text box that says "mod loaded!" when the player first spawns. The game code behind those buttons lives in `src/logic.cpp`.

The example looks for these optional files in the game's `Android/data/<package>/files` folder, then in the AML config folder:

- `button.png` (or `.jpg`/`.jpeg`) for the launcher. Without it the launcher is a text button.
- `font.ttf` or `font.otf` for the trainer window and the text box.
- `music.mp3` or `music.wav` for the Music tab.

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
