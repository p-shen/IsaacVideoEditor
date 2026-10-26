#!/usr/bin/env bash
# The editor renders text/visuals itself; its required MLT services avoid mixing Qt versions.
set -euo pipefail
[[ $# == 3 && ( $3 == so || $3 == dll ) ]] || { echo "Usage: $0 SOURCE_DIR DESTINATION_DIR so|dll" >&2; exit 1; }
for module in core avformat normalize resample sdl2 xml; do
    source="$1/libmlt$module.$3"
    [[ -f "$source" ]] || { echo "Required MLT $module plugin is missing: $source" >&2; exit 1; }
    cp -L "$source" "$2/"
done
