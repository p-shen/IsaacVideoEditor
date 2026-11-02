# Release packaging

The release workflow builds the `IsaacVideoEditor` CMake target with `BUILD_TESTING=OFF`, then calls the platform script with `BUILD_DIR OUTPUT_DIR VERSION`. Scripts stage files in a temporary directory, reject unsafe version names and existing output files, and leave the original build unchanged.

```sh
python3 packaging/package-macos.py build-release dist v0.1.0
bash packaging/package-linux.sh build-release dist v0.1.0
bash packaging/package-windows.sh build-release dist v0.1.0
```

Run only the command for the host platform. macOS uses Homebrew and `macdeployqt`; Linux uses Qt 6.8.3, MLT/FFmpeg packages, linuxdeploy and its Qt/AppImage plugins; Windows uses the MSYS2 UCRT64 toolchain, `windeployqt`, and Inno Setup 6 (`ISCC.exe` on PATH or in its standard Program Files (x86) directory). Exact dependency installation and pinned deployment tools are in [the workflow](../.github/workflows/release.yml).

Windows produces both the portable ZIP and `IsaacVideoEditor-VERSION-windows-x86_64-Setup.exe` from the same staged payload. `APP_VERSION` supplies the numeric application version when `VERSION` is a CI artifact label. Setup installs for the current user, adds a Start menu shortcut and an optional desktop shortcut, and registers an uninstaller. The portable ZIP remains the in-app update payload; installer metadata is outside its manifest and is preserved by the update helper. Inno's uninstaller removes the files recorded during setup, without recursively deleting user-added files.

## Runtime layout

| Platform | Executables | MLT modules | MLT data |
| --- | --- | --- | --- |
| macOS app | `Contents/MacOS` | `Contents/PlugIns/mlt-7` | `Contents/Resources/mlt-7` |
| Linux AppDir | `usr/bin` | `usr/lib/mlt-7` | `usr/share/mlt-7` |
| Windows folder | application root | `lib/mlt-7` | `share/mlt-7` |

`bundledRuntimePaths` detects these paired module/data folders. MLT receives its bundled module path explicitly, and the executable folder is added to the tool search path. Development builds without that layout retain system dependency discovery.

Required MLT modules are core, avformat, normalize, resample, sdl2, and xml. The application supplies its own Qt text and visual rendering, so MLT's optional Qt modules are excluded. Platform scripts scan the dependency closure of the required modules and both FFmpeg tools, including dynamically loaded modules that an executable-only scan would miss. macOS also includes SDL3 when SDL2 is supplied through sdl2-compat.

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
