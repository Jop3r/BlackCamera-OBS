# BlackCamera OBS

**BlackCamera ASCII**: a video filter for OBS Studio that turns any source (video capture device, window capture, media) into real-time ASCII art. It is the effect of [BlackCamera](https://github.com/Jop3r/BlackCamera) as an OBS plugin: a glyph atlas renderer (DirectWrite + multithreaded CPU compositing), face-tracking font zoom (YuNet) and the Cyber HUD.

[🇷🇺 Русская версия](README_RU.md)

[![build](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml/badge.svg)](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml)

> Experimental. Windows x64 only, built and tested against OBS Studio 32.2.

---

## Install

1. Download `blackcamera-obs-<version>-windows-x64.zip` from [Releases](https://github.com/Jop3r/BlackCamera-OBS/releases).
2. Close OBS and extract the archive into `%ProgramData%\obs-studio\plugins` (paste the path into the Explorer address bar). You get `%ProgramData%\obs-studio\plugins\blackcamera-obs`.
3. Start OBS.

This folder works for any OBS installation (including Steam) and does not touch the OBS program folder. To uninstall, delete `%ProgramData%\obs-studio\plugins\blackcamera-obs`.

Every push to `master` also builds the plugin: the zip is in the **Artifacts** of the [build run](https://github.com/Jop3r/BlackCamera-OBS/actions/workflows/build.yml).

## Build

Requirements: Windows 10/11 x64, OBS Studio installed, Visual Studio 2022 or newer with the **Desktop development with C++** workload, [vcpkg](https://github.com/microsoft/vcpkg). OpenCV is declared in `vcpkg.json`, installed automatically and linked statically, so the plugin is a single DLL.

1. Prepare the libobs SDK for the installed OBS version (once, and again after updating OBS):

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\obs-sdk.ps1
   ```

   The script takes the libobs headers from the obs-studio sources at the tag of the installed OBS (sparse git checkout) and generates `obs.lib` from the exports of the installed `obs.dll`, so OBS itself does not have to be built. The result is in `.deps\obs-sdk`. Pass `-ObsDir` / `-Version` if OBS is not found or you want another version. A real libobs CMake package (an obs-studio build, obs-plugintemplate's `.deps`) can be used instead with `-Dlibobs_DIR=...`.

2. Build:

   ```powershell
   $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
   cmake --workflow --preset release
   ```

   `cmake` must be on `PATH` (or use **Developer PowerShell for VS**). The first build compiles OpenCV through vcpkg and can take a while; later builds use the vcpkg binary cache.

The plugin is staged in `build\stage\Release\blackcamera-obs`:

```
blackcamera-obs\
  bin\64bit\blackcamera-obs.dll
  data\locale\en-US.ini, ru-RU.ini
  data\face_detection_yunet_2023mar.onnx
```

Install the build (OBS closed):

```powershell
cmake --install build --config Release --prefix "$env:ProgramData\obs-studio\plugins"
```

`tools\package.ps1` packs it into `dist\blackcamera-obs-<version>-windows-x64.zip`, the same archive the release has. A tag `v<version>` (the version in `CMakeLists.txt`) makes CI publish a release with it.

## Use

Right-click a source → **Filters** → **+** → **BlackCamera ASCII**.

| Setting | Meaning |
|---------|---------|
| Brightness (-100..100), Contrast (0.1..3.0) | Applied to the source before conversion |
| Blur Kernel Size (1..31, odd) | Smooths the image before it is mapped to characters |
| Min Brightness Threshold (0..255) | Darker cells stay empty |
| Color Mode | Custom / Camera / Matrix Green / Amber CRT / Cyberpunk / Rainbow |
| Antialiasing | Default / ClearType / Grayscale / Aliased (retro sharp) |
| Character Preset | Sparse / Standard / Classic Unicode / Binary / Matrix Rain / Custom |
| Font Size (min) | Font size; the minimum when face zoom is on |
| Scale font with source height | Font sizes are for a 1080-px-high picture and scaled to the source height (on by default) |
| Face Tracking Font Zoom | Max Font, Zoom Speed, Cyber HUD, Jitter Deadzone, Max Sensitivity, Min Threshold |

Plugin messages are in the OBS log (**Help → Log Files**) with the `[blackcamera-obs]` prefix.

## How it works

The filter renders its source into a texture, reads it back through two staging surfaces (the one mapped was staged a frame earlier, so the GPU is not stalled) and hands it to a worker thread. The worker runs the face tracker and the ASCII renderer on the CPU; the latest result is uploaded into a dynamic texture and drawn instead of the source. A slow frame lowers the effect's frame rate, never OBS's. The output keeps the source's size.

The source is captured in SDR sRGB (OBS tone-maps HDR sources into it) and the result is drawn as an sRGB source, so OBS converts it correctly for SDR, 16-bit and HDR canvases.

## Source layout

| Path | Responsibility |
|------|----------------|
| `src/plugin-main.cpp` | Module entry point, OBS log sink |
| `src/ascii_filter.*` | The filter: properties, GPU readback, worker thread, drawing |
| `src/core/ascii_renderer.*` | Glyph atlas, frame compositing, HUD drawing |
| `src/core/face_tracker.*` | YuNet detection, font zoom, HUD smoothing |
| `src/core/settings.*` | Effect settings, character presets |
| `src/core/log.*`, `src/core/platform.*` | Logging, UTF-8 helpers |
| `tools/obs-sdk.ps1` | Minimal libobs SDK from the installed OBS |
| `tools/package.ps1` | Release zip from the staged build |

`src/core` comes from [BlackCamera](https://github.com/Jop3r/BlackCamera) (`BlackCamera/`), without the app's camera, virtual camera and settings file code.

## License

The source code is MIT-licensed, see [LICENSE](LICENSE). The plugin links against libobs (GPL-2.0-or-later), so a plugin binary is distributed under the GPL. Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
