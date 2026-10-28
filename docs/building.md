# Build from source

The Apple silicon Mac build has been verified. Intel Mac, Linux, and Windows builds remain unverified; the instructions below describe the dependency setup and build commands for each platform.

All platforms need Git, a C++17 compiler, CMake 3.21 or newer, pkg-config, MLT 7 with its C++ development files and FFmpeg/avformat plugin, and FFmpeg's `ffmpeg` and `ffprobe` commands. Qt must include Core, Gui, Widgets, Quick, Qml, QuickControls2, Network, and Test, plus the QML runtime modules and SVG image plugin. Use Qt **6.6 or newer**: the interface uses [`font.features`](https://doc.qt.io/qt-6/qml-qtquick-text.html#font.features-prop), introduced in Qt 6.6, even though CMake currently accepts older Qt 6 versions.

Build and run on the same machine with the dependencies installed. These commands produce a development build that uses those installed libraries; copying the executable alone to another machine does not include its dependencies.

### macOS

Install Apple's Command Line Tools if needed:

```sh
xcode-select --install
```

Install [Homebrew](https://brew.sh/) and follow its instructions to make `brew` available in your terminal. Then install the tools and libraries, including [Qt](https://formulae.brew.sh/formula/qt) and [MLT](https://formulae.brew.sh/formula/mlt):

```sh
brew install git cmake pkgconf qt mlt ffmpeg
git clone https://github.com/p-shen/IsaacVideoEditor.git
cd IsaacVideoEditor
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --target IsaacVideoEditor --parallel 4
open build/IsaacVideoEditor.app
```

If you already cloned the repository, use that folder and skip `git clone`. Keep the compiler and Homebrew dependencies on the same architecture: arm64 on Apple silicon, or x86_64 on Intel. Intel Macs also depend on Homebrew support for their macOS version and packages.

### Linux

The following example uses a Debian 13 (Trixie) desktop, which provides [Qt 6.8 development packages](https://packages.debian.org/trixie/qt6-declarative-dev) and [MLT 7 C++ development packages](https://packages.debian.org/trixie/libmlt%2B%2B-dev). Run the app from a graphical desktop session.

```sh
sudo apt update
sudo apt install git build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
  qml6-module-qtquick qml6-module-qtquick-window \
  qml6-module-qtquick-controls qml6-module-qtquick-templates \
  qml6-module-qtquick-layouts qml6-module-qtqml-workerscript \
  qml6-module-qtqml-models qt6-svg-plugins \
  libmlt-dev libmlt++-dev libmlt-data ffmpeg
git clone https://github.com/p-shen/IsaacVideoEditor.git
cd IsaacVideoEditor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target IsaacVideoEditor --parallel 4
./build/IsaacVideoEditor
```

If you already cloned the repository, skip `git clone`. On other distributions, install equivalent development libraries, QML modules, and runtime plugins through the distribution's package manager. Check the Qt version before using an older distribution's packages: Qt 6.2 or 6.4 is too old for the current interface. For a separate Qt installation, pass its installation prefix using `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64` when configuring, and use matching Qt libraries and plugins at runtime.

### Windows

Use a 64-bit x86 Windows machine with [MSYS2](https://www.msys2.org/) and its **UCRT64** terminal. This keeps the compiler, Qt, MLT, and FFmpeg in one compatible environment. The commands below are Bash commands for that terminal; do not run them in PowerShell or Command Prompt, or mix these packages with an MSVC Qt installation.

First [update MSYS2](https://www.msys2.org/docs/updating/):

```sh
pacman -Syu
```

If the update asks you to close the terminal, close it, reopen **MSYS2 UCRT64**, and run `pacman -Syu` again to finish updating. Then install the build tools, [Qt Quick/QML](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-qt6-declarative), and [MLT](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-mlt):

```sh
pacman -S --needed git \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-qt6-5compat \
  mingw-w64-ucrt-x86_64-mlt \
  mingw-w64-ucrt-x86_64-ffmpeg
git clone https://github.com/p-shen/IsaacVideoEditor.git
cd IsaacVideoEditor
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/ucrt64
cmake --build build --target IsaacVideoEditor --parallel 4
./build/IsaacVideoEditor.exe
```

If you already cloned the repository, skip `git clone`. Launch from the UCRT64 terminal so `/ucrt64/bin` remains on `PATH` for Qt, MLT, FFmpeg, and their DLLs. This setup does not cover native Windows ARM64 or MSVC builds.

### Dependency checks and common setup problems

Run these in the same terminal used to build and launch the app:

```sh
cmake --version
pkg-config --modversion mlt++-7
ffmpeg -version
ffprobe -version
```

- **CMake cannot find Qt6:** install the Qt development packages, including Quick/QML and Test. If Qt is outside the package manager's usual location, set `CMAKE_PREFIX_PATH` to its installation prefix. Check that the selected Qt is at least 6.6.
- **CMake cannot find `mlt++-7`:** install MLT's C++ development package. For a custom MLT installation, add the directory containing `mlt++-7.pc` to `PKG_CONFIG_PATH`; the `pkg-config` check above must succeed.
- **A QML module is missing or icons are blank:** install the QML runtime modules and Qt SVG plugin from the same Qt installation used to build the app. C++ development libraries alone do not supply every runtime plugin on all distributions.
- **Media import or export fails:** confirm both FFmpeg commands are available and MLT's avformat plugin is installed. Installing the FFmpeg command alone does not supply a missing MLT plugin.
- **Windows reports a missing DLL:** launch from UCRT64 and confirm the dependencies use the `mingw-w64-ucrt-x86_64-` prefix.
- **Changing compiler, architecture, generator, or Qt installation:** configure into a new build directory rather than reusing a cache from another setup.

The commands above build the application target. To build and run the test suite, use `cmake --build build --parallel 4` followed by `ctest --test-dir build --output-on-failure`. Some tests currently hardcode `/opt/homebrew/bin/ffmpeg`, so test failures on other setups can reflect those paths. Linux and Windows build, launch, and media checks remain outstanding.

