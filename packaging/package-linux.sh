#!/usr/bin/env bash
# Ubuntu 24.04 x86_64; linuxdeploy and its Qt/AppImage plugins are provisioned by CI.
set -euo pipefail
export LC_ALL=C
fail() { echo "Linux packaging: $*" >&2; exit 1; }
[[ $# == 3 ]] || fail "Usage: $0 BUILD_DIR OUTPUT_DIR VERSION"
build_dir=$1
output_dir=$2
version=$3
[[ $version =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ && ${#version} -le 100 ]] || fail "Version must be 1-100 filename-safe characters, starting with a letter or digit"
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || fail "Run on Linux x86_64 (Ubuntu 24.04 or newer)"
for tool in pkg-config ffmpeg ffprobe ldd readelf dpkg-query; do
    command -v "$tool" >/dev/null || fail "Missing dependency: $tool"
done
linuxdeploy=${LINUXDEPLOY:-linuxdeploy}
command -v "$linuxdeploy" >/dev/null || fail "Install linuxdeploy with its Qt and AppImage plugins, or set LINUXDEPLOY"
linuxdeploy=$(command -v "$linuxdeploy")
linuxdeploy="$(CDPATH= cd -- "$(dirname -- "$linuxdeploy")" && pwd)/$(basename -- "$linuxdeploy")"
qmake=${QMAKE:-qmake6}
command -v "$qmake" >/dev/null || fail "Install Qt 6 development tools, or set QMAKE to its qmake executable"
export QMAKE
QMAKE=$(command -v "$qmake")
QMAKE="$(CDPATH= cd -- "$(dirname -- "$QMAKE")" && pwd)/$(basename -- "$QMAKE")"
qt_prefix=$("$QMAKE" -query QT_INSTALL_PREFIX)
[[ -x "$build_dir/IsaacVideoEditor" ]] || fail "Missing executable: $build_dir/IsaacVideoEditor"
[[ -x "$build_dir/IsaacVideoEditorUpdater" ]] || fail "Missing update installer: $build_dir/IsaacVideoEditorUpdater"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
filename="IsaacVideoEditor-$version-linux-x86_64.AppImage"
[[ ! -e "$output_dir/$filename" && ! -L "$output_dir/$filename" ]] || fail "Output already exists: $output_dir/$filename"
work=$(mktemp -d "${TMPDIR:-/tmp}/isaac-linux.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
appdir="$work/AppDir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/lib/mlt-7" "$appdir/usr/share/mlt-7" "$appdir/usr/share/licenses/IsaacVideoEditor" "$appdir/usr/share/applications" "$appdir/apprun-hooks"
cp "$repo/packaging/linux-runtime-hook.sh" "$appdir/apprun-hooks/00-isaacvideoeditor.sh"

mlt_libdir=$(pkg-config --variable=libdir mlt-framework-7)
mlt_prefix=$(pkg-config --variable=prefix mlt-framework-7)
mlt_plugins=$(pkg-config --variable=moduledir mlt-framework-7)
if [[ ! -d "$mlt_plugins" ]]; then
    for candidate in "$mlt_libdir/mlt-7" "$mlt_libdir/mlt"; do
        if [[ -d "$candidate" ]]; then mlt_plugins=$candidate; break; fi
    done
fi
mlt_data=$(pkg-config --variable=mltdatadir mlt-framework-7)
if [[ ! -d "$mlt_data" ]]; then
    for candidate in "$mlt_prefix/share/mlt-7" "$mlt_prefix/share/mlt"; do
        if [[ -d "$candidate" ]]; then mlt_data=$candidate; break; fi
    done
fi
[[ -d "$mlt_plugins" && -d "$mlt_data/profiles" ]] || fail "Cannot find MLT 7 plugins and profile data via pkg-config"
bash "$repo/packaging/copy-mlt-modules.sh" "$mlt_plugins" "$appdir/usr/lib/mlt-7/" so
cp -aL "$mlt_data/." "$appdir/usr/share/mlt-7/"
cp "$build_dir/IsaacVideoEditor" "$build_dir/IsaacVideoEditorUpdater" "$appdir/usr/bin/"
cp "$(command -v ffmpeg)" "$(command -v ffprobe)" "$appdir/usr/bin/"
cp "$repo/LICENSE" "$appdir/usr/share/licenses/IsaacVideoEditor/LICENSE"
cat > "$appdir/usr/share/applications/IsaacVideoEditor.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=IsaacVideoEditor
Exec=IsaacVideoEditor %F
Icon=IsaacVideoEditor
Categories=AudioVideo;Video;AudioVideoEditing;
Terminal=false
DESKTOP

# Explicitly scan every MLT module: these are loaded dynamically, not linked by the app.
deploy_args=(--appdir "$appdir" --executable "$appdir/usr/bin/IsaacVideoEditor" --executable "$appdir/usr/bin/ffmpeg" --executable "$appdir/usr/bin/ffprobe")
while IFS= read -r -d '' module; do
    deploy_args+=(--deploy-deps-only "$module")
done < <(find "$appdir/usr/lib/mlt-7" -type f -name '*.so*' -print0)
# Offscreen supports CI smoke checks; SVG icons are loaded at runtime without QtSvg linkage.
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:+$EXTRA_PLATFORM_PLUGINS;}libqoffscreen.so"
export EXTRA_QT_MODULES="${EXTRA_QT_MODULES:+$EXTRA_QT_MODULES;}svg"
export QML_SOURCES_PATHS="$repo/qml"
export APPIMAGE_EXTRACT_AND_RUN=1
"$linuxdeploy" "${deploy_args[@]}" --plugin qt \
    --desktop-file "$appdir/usr/share/applications/IsaacVideoEditor.desktop" \
    --icon-file "$repo/assets/isaac-logo-96.png" --icon-filename IsaacVideoEditor
for plugin in platforms/libqoffscreen.so imageformats/libqsvg.so; do
    [[ -f "$appdir/usr/plugins/$plugin" ]] || fail "Qt deployment did not include required plugin: $plugin"
done

# linuxdeploy collects dependency copyrights; retain distro notices and the exact SDK's notices too.
licenses="$appdir/usr/share/licenses"
mkdir -p "$licenses/debian"
for copyright in /usr/share/doc/*/copyright; do
    [[ -f "$copyright" ]] || continue
    package=${copyright%/copyright}
    package=${package##*/}
    cp -L "$copyright" "$licenses/debian/$package.copyright"
done
cp -aL /usr/share/common-licenses "$licenses/"
dpkg-query -W '-f=${Package}\t${Version}\n' > "$licenses/build-packages.tsv"
qt_license_dir=${QT_LICENSE_DIR:-}
if [[ -z "$qt_license_dir" ]]; then
    for candidate in "$qt_prefix/LICENSES" "$qt_prefix/../../Licenses"; do
        if [[ -d "$candidate" ]]; then qt_license_dir=$candidate; break; fi
    done
fi
[[ -d "$qt_license_dir" ]] || fail "Qt SDK license notices not found; set QT_LICENSE_DIR to the matching Qt source LICENSES/notices directory"
cp -aL "$qt_license_dir" "$licenses/Qt"

# Check all ELF files, including Qt/QML plugins added during deployment.
while IFS= read -r -d '' binary; do
    readelf -h "$binary" >/dev/null 2>&1 || continue
    dependencies=$(LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$binary" 2>&1) || fail "Cannot inspect ELF dependencies: $binary: $dependencies"
    [[ $dependencies != *'not found'* ]] || fail "Unresolved ELF dependency: $binary: $dependencies"
done < <(find "$appdir/usr" -type f \( -perm /111 -o -name '*.so*' \) -print0)
(
    cd "$work"
    LDAI_OUTPUT="$filename" ARCH=x86_64 "$linuxdeploy" --appdir "$appdir" --output appimage
)
[[ -s "$work/$filename" ]] || fail "linuxdeploy did not produce $filename"
chmod +x "$work/$filename"
mv -- "$work/$filename" "$output_dir/$filename"
echo "$output_dir/$filename"
