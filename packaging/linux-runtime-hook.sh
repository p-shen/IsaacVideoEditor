#!/bin/sh
# Sourced before Qt hooks by linuxdeploy's AppRun, which defines this_dir.
export APPDIR="$this_dir"
export PATH="$APPDIR/usr/bin:$PATH"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export MLT_REPOSITORY="$APPDIR/usr/lib/mlt-7"
export MLT_DATA="$APPDIR/usr/share/mlt-7"
export MLT_PROFILES_PATH="$MLT_DATA/profiles"
