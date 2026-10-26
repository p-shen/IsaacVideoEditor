#!/usr/bin/env bash
# Run in the MSYS2 UCRT64 shell with native MinGW Qt, MLT, and FFmpeg packages.
set -euo pipefail
export LC_ALL=C
fail() { echo "Windows packaging: $*" >&2; exit 1; }
[[ $# == 3 ]] || fail "Usage: $0 BUILD_DIR OUTPUT_DIR VERSION"
build_dir=$1
output_dir=$2
version=$3
[[ $version =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ && ${#version} -le 100 ]] || fail "Version must be 1-100 filename-safe characters, starting with a letter or digit"
[[ ${MSYSTEM:-} == UCRT64 ]] || fail "Run from an MSYS2 UCRT64 shell"
prefix=${MINGW_PREFIX:-/ucrt64}
for tool in pkg-config windeployqt objdump python3 pacman zip; do
    command -v "$tool" >/dev/null || fail "Missing dependency: $tool"
done
[[ -x "$build_dir/IsaacVideoEditor.exe" ]] || fail "Missing executable: $build_dir/IsaacVideoEditor.exe"
[[ -f "$prefix/bin/ffmpeg.exe" && -f "$prefix/bin/ffprobe.exe" ]] || fail "Install the UCRT64 FFmpeg package"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(CDPATH= cd -- "$build_dir" && pwd)
mkdir -p -- "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
name="IsaacVideoEditor-$version-windows-x86_64"
[[ ! -e "$output_dir/$name.zip" && ! -L "$output_dir/$name.zip" ]] || fail "Output already exists: $output_dir/$name.zip"
work=$(mktemp -d "${TMPDIR:-/tmp}/isaac-windows.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
stage="$work/IsaacVideoEditor"
mkdir -p "$stage/lib/mlt-7" "$stage/share/mlt-7" "$stage/licenses"
mlt_plugins=$(pkg-config --variable=moduledir mlt-framework-7)
if [[ ! -d "$mlt_plugins" ]]; then
    for candidate in "$prefix/lib/mlt-7" "$prefix/lib/mlt"; do
        if [[ -d "$candidate" ]]; then mlt_plugins=$candidate; break; fi
    done
fi
mlt_data=$(pkg-config --variable=mltdatadir mlt-framework-7)
if [[ ! -d "$mlt_data" ]]; then
    for candidate in "$prefix/share/mlt-7" "$prefix/share/mlt"; do
        if [[ -d "$candidate" ]]; then mlt_data=$candidate; break; fi
    done
fi
[[ -d "$mlt_plugins" && -d "$mlt_data/profiles" ]] || fail "Cannot find MLT 7 plugins and profile data via pkg-config"
bash "$repo/packaging/copy-mlt-modules.sh" "$mlt_plugins" "$stage/lib/mlt-7/" dll
cp -aL "$mlt_data/." "$stage/share/mlt-7/"
cp "$build_dir/IsaacVideoEditor.exe" "$prefix/bin/ffmpeg.exe" "$prefix/bin/ffprobe.exe" "$stage/"
cp "$repo/LICENSE" "$stage/licenses/IsaacVideoEditor.txt"
windeployqt --release --compiler-runtime --svg --include-plugins qoffscreen,qsvg --qmldir "$repo/qml" --dir "$stage" "$stage/IsaacVideoEditor.exe"
for plugin in platforms/qoffscreen.dll imageformats/qsvg.dll; do
    [[ -f "$stage/$plugin" ]] || fail "Qt deployment did not include required plugin: $plugin"
done
python3 "$repo/packaging/windows-dll-closure.py" "$stage" "$prefix" "$(command -v objdump)"
[[ -d "$prefix/share/licenses" ]] || fail "Missing UCRT64 package license notices"
cp -aL "$prefix/share/licenses" "$stage/licenses/UCRT64"
if [[ -d /usr/share/licenses/common ]]; then
    cp -aL /usr/share/licenses/common "$stage/licenses/"
fi
pacman -Q > "$stage/licenses/build-packages.txt"
cat > "$stage/qt.conf" <<'CONF'
[Paths]
Prefix=.
Plugins=.
QmlImports=qml
CONF
(
    cd "$work"
    zip -qr "$name.zip" IsaacVideoEditor
)
[[ -s "$work/$name.zip" ]] || fail "ZIP creation failed"
mv -- "$work/$name.zip" "$output_dir/$name.zip"
echo "$output_dir/$name.zip"
