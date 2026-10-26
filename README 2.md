# IsaacVideoEditor

A small, local video editor for short vlogs. It uses C++17, Qt Quick/QML, MLT, and FFmpeg through MLT. There is no account or paid license.

## Build on an Apple silicon Mac

Install the build tools and media engine with Homebrew:

```sh
brew install cmake qt mlt ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
open build/IsaacVideoEditor.app
```

## First edit

1. Drag video files from Finder onto the window, or click **Import**. Videos are added directly to the edit.
2. Select a clip in the left browser or bottom timeline. In the **Clip** inspector, set **Start** and **Length** (for example, Start 5 s and Length 10 s keeps seconds 5–15), or drag the selected clip's edges. Lengths round to the nearest frame.
3. To make multiple pieces, move the playhead inside the selected clip and click **Split**, or press **Command+B** on Mac (**Ctrl+B** on Windows/Linux). Repeat as needed. Drag pieces to reorder them; **Earlier**/**Later** work too. Split points stay straight cuts, and the source file stays intact.
4. Use **Audio** for recorded narration and background music. In **Background music**, click **Browse Mixkit** to open its free music library in your browser and download a track. Click **Recent downloads** and select the track to add it; the menu refreshes with the ten newest audio files in your system Downloads folder. **Choose** opens audio files from any folder. Keep downloaded source files in place while your project uses them. Use **Fades** for crossfades between imported clips. Splitting inside an active crossfade is disabled; move past the overlap or set fades to 0.
5. Click the timeline ruler to seek, press **Space** to preview, save the project, then use **Export** in the header.

Open, import, audio selection, save, **Save As**, and export dialogs start in `~/Movies`. **Save As** keeps the project's filename as a suggestion. Saving an existing project directly keeps its current location.

Projects save as `.isaac.json`. To open an existing project with another JSON filename, select **JSON files** in the Open dialog. The project file is JSON and references the original media by its full path. Keep source files in place after saving. Export uses H.264 video and AAC audio at 60 fps. Resolution follows the smallest original video in the edit: mixed 4K and 2560×1440 clips export at 2560×1440; all-4K footage exports at 4K. The header shows the exact export dimensions, including formats sometimes called 2K or 2.7K. The canvas stays landscape, and other clips fit inside it without cropping. Existing 30 fps projects retain their trim and crossfade durations when opened. New saves use version 4 to retain rotation and straight split boundaries; this app also loads versions 1–3. Narration is imported from an audio file; microphone recording is not built in. Music repeats until the video ends.

To change a clip's orientation, select it and use **Clip → Rotate → Left 90°**, **Right 90°**, or **Turn 180°**. Turns accumulate and apply to that clip only. The whole image fits inside the video canvas, adding black bars when needed. Rotation is saved with the project and appears in both preview and export; split pieces inherit it.

## Interface

The resizable workspace places the clip browser on the left, the viewer in the center, the inspector on the right, and the timeline below. In narrow windows the inspector becomes a tab beside Clips. Short windows with large text scroll the workspace. Both light and dark appearances use the system font.

For videos larger than 1080p, the app prepares small preview copies in the background and caches them locally. It prepares the portions used in the edit, with small timing margins, and prioritizes the clip at the playhead. Existing copies are reused when they cover the needed footage. Source timestamps are checked in the background before cutting off unused beginnings; variable or uncertain timing retains the original beginning to keep preview frames aligned. The viewer shows preparation progress. Editing uses these copies once ready; export always reads the originals at the selected native resolution. Preview copies preserve source timing, audio, and orientation, and use extra disk space in the app's local cache. Until preparation finishes, preview uses the originals and may still stutter with demanding footage. If a copy is unavailable, the app falls back to the original.

On macOS, export uses Apple's hardware H.264 encoder when available and automatically retries with software encoding if needed. Hardware export uses a bitrate suited to the output dimensions; file sizes can differ from software export. Background preview preparation pauses during export and resumes afterward, including if export fails.

**Undo/Redo** restore imports, order, trims, splits, rotation, fades, and audio settings. Editing preserves the preview position when possible. Use **Tab** to move between controls; the position slider and timeline support arrow keys for one frame and Page Up/Down for one second. The time display uses minutes:seconds:frames at 60 fps. **View** has shortcuts to focus each editing area; **Fit** shows the edit and **−/+** change timeline zoom.

The source is set up for Qt on macOS, Linux, and Windows. This Mac build is verified; Windows and Linux builds still need a machine with Qt 6, MLT 7, CMake, and pkg-config development files to verify them.

The Qt Quick interface adds source filmstrips and audio waveforms to the timeline. **Waveforms 1×/2×/4×** changes their display scale without changing audio gain. Missing audio is shown as an unavailable waveform, rather than a silent signal. Use **View → Retry previews** to retry derived media.

Selecting a clip keeps the playhead in place. Drag the ruler or playback slider to scrub; **Escape** cancels and restores the previous position and playback state. **Skim** silently previews under the pointer while paused and restores the playhead frame when the pointer leaves. Pinch or Command/Ctrl+wheel zooms around the pointer; the zoom buttons center on the playhead. Horizontal scrolling pans the timeline.

Trim either clip edge directly. The viewer compares the outgoing and incoming source frames, and the timeline previews ripple positions and any globally shortened crossfades. **N** toggles snapping while the timeline has focus; holding **Option/Alt** temporarily bypasses it. **Command/Ctrl-click** toggles clips and **Shift-click** selects a range. Move or remove the selected clips as a group; the **Selection** menu provides Select All, Select Range, and Clear Selection. Each accepted gesture is one undo step; canceled and unchanged gestures create none.

The [universal design notes](docs/universal-design.md) record the color, keyboard, and layout rules used by the app.

## Qt Quick implementation

The production interface now uses Qt Quick/QML and Qt Quick Controls, retaining the C++/MLT media engine and project versions 1–4. Native file/message dialogs still use Qt Widgets; the old Widgets window is retained only for regression comparisons. The [interaction design](docs/superpowers/specs/2026-10-25-qt-quick-interaction-design.md) records the roadmap, and the [implementation and verification notes](docs/qt-quick-migration.md) describe priorities 1–3, performance measurements, and remaining hardware checks.
