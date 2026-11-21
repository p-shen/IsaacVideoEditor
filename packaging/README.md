# Release packaging

The release workflow builds the `IsaacVideoEditor` CMake target with `BUILD_TESTING=OFF`, then calls the platform script with `BUILD_DIR OUTPUT_DIR VERSION`. Scripts stage files in a temporary directory, reject unsafe version names and existing output files, and leave the original build unchanged.

```sh
python3 packaging/package-macos.py build-release dist v0.1.0
bash packaging/package-linux.sh build-release dist v0.1.0
bash packaging/package-windows.sh build-release dist v0.1.0
```

Run only the command for the host platform. macOS uses Homebrew and `macdeployqt`; Linux uses Qt 6.8.3, MLT/FFmpeg packages, linuxdeploy and its Qt/AppImage plugins; Windows uses the MSYS2 UCRT64 toolchain, `windeployqt`, and Inno Setup 6 (`ISCC.exe` on PATH or in its standard Program Files (x86) directory). Exact dependency installation and pinned deployment tools are in [the workflow](../.github/workflows/release.yml).

Windows produces both the portable ZIP and `IsaacVideoEditor-VERSION-windows-x86_64-Setup.exe` from the same staged payload. `APP_VERSION` supplies the numeric application version when `VERSION` is a CI artifact label. Setup installs for the current user, adds a Start menu shortcut and an optional desktop shortcut, and registers an uninstaller. The portable ZIP remains the in-app update payload; installer metadata is outside its manifest and is preserved by the update helper. Inno's uninstaller removes the files recorded during setup, then removes files introduced by updates using the current manifest. This cleanup rejects unsafe paths and junctions and only removes empty directories, preserving user-added files.

## Runtime layout

Release builds also prepare the pinned local subtitle engine with `python3 packaging/prepare-whisper.py build-whisper` and configure `ISAAC_WHISPER_RUNTIME_DIR` to that absolute folder. Mac/Linux compile a standalone v1.9.5 worker; Windows uses the official b5454 CUDA 12.4 archive plus an isolated CPU worker. Download digests are fixed in the preparation script. Windows preparation needs the Windows SDK manifest tool and Visual Studio 2022 CRT redistributables; the worker's UTF-8 manifest preserves non-ASCII filenames on Windows 10 version 1903+ and Windows 11. Native libraries and license notices remain inside the private runtime directory (`Contents/Resources/subtitles` on Mac, `subtitles` beside the executable on Windows/Linux). Model weights are not part of the package.

Windows dependency collection validates worker DLLs beside each worker, keeping their runtime separate from the editor's UCRT64/Qt DLLs. On Mac, the existing dependency relocation and signing scan includes the worker. `runtime.json` records preparation provenance; final package relocation/signing may change executable bytes. Physical NVIDIA acceptance remains separate from packaging on a runner without an NVIDIA GPU.

| Platform | Executables | MLT modules | MLT data |
| --- | --- | --- | --- |
| macOS app | `Contents/MacOS` | `Contents/PlugIns/mlt-7` | `Contents/Resources/mlt-7` |
| Linux AppDir | `usr/bin` | `usr/lib/mlt-7` | `usr/share/mlt-7` |
| Windows folder | application root | `lib/mlt-7` | `share/mlt-7` |

`bundledRuntimePaths` detects these paired module/data folders. MLT receives its bundled module path explicitly, and the executable folder is added to the tool search path. Development builds without that layout retain system dependency discovery.

Required MLT modules are core, avformat, normalize, resample, sdl2, and xml. The application supplies its own Qt text and visual rendering, so MLT's optional Qt modules are excluded. Platform scripts scan the dependency closure of the required modules and both FFmpeg tools, including dynamically loaded modules that an executable-only scan would miss. macOS and Windows also include SDL3 when SDL2 is supplied through sdl2-compat.

## Checks

The workflow extracts each archive or runs the AppImage with `tests/package_smoke_test.py`. The executable's `--smoke-test FIXTURE` entry point checks QML loading, SVG support, both FFmpeg tools, media decoding, SDL playback, and MP4 export. `ISAAC_CLEAN_RUNTIME=1` removes development tool/library environment paths for the checked process. Smoke checks require the bundled tools when a bundle layout is detected.

Platform helper tests use Python's standard library:

```sh
python3 -m unittest discover -s packaging -p 'test_*.py'
python3 -m unittest discover -s packaging/tests -p 'test_*.py'
```

The macOS dependency tests require macOS development tools. For Linux's official Qt SDK, `QT_LICENSE_DIR` points at notices collected from the matching Qt source version; the workflow provisions it. Runtime package inventories and license notices are retained inside the downloads.

Windows CI also runs `tests/windows_installer_smoke_test.py SETUP_EXE PORTABLE_ZIP` to check install, reinstall, shortcut targets, the installed app, preservation of uninstaller files during an actual in-app update, and uninstallation without deleting a user-added project. This script refuses to run outside a disposable Windows GitHub Actions runner.

See [publishing and validating releases](../docs/releases.md) for the user-facing process and signing limitations.
