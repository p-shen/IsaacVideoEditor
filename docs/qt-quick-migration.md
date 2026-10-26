# Qt Quick migration and interaction verification

IsaacVideoEditor's production window now uses Qt Quick/QML and Qt Quick Controls. The C++/MLT editing engine, original-source export, 60 fps timing, and project versions 1–4 remain in place. The first three priorities from the [interaction design](superpowers/specs/2026-10-25-qt-quick-interaction-design.md) are implemented.

## Editing interactions

| Priority | Implemented behavior |
| --- | --- |
| Filmstrips and waveforms | Visible-source thumbnails respect trim, source cadence, and orientation. Video, narration, and looping-music waveforms use fixed-amplitude peaks. Display magnification offers 1×, 2×, and 4× without changing gain. Background preparation, unavailable states, retry, source fingerprinting, bounded caches, and viewport queue retirement keep the UI responsive. |
| Navigation and preview | Ruler and slider scrubbing preserve or restore playback state. Asynchronous still decoding coalesces requests and rejects stale frames. Optional silent skimming is independent of the committed playhead. Pointer-anchored pinch/modified-wheel zoom, playhead-anchored buttons, panning, and edge scrolling preserve timeline context. |
| Precision editing | Edge trims preview the complete resulting sequence and global crossfade changes. Two-up source-frame comparison receives priority cache requests during the drag. Snapping uses 8-pixel acquisition and 12-pixel release with an Option/Alt override. Modifier/range selection, ordered group moves, removal, keyboard/numeric alternatives, and one-step undo share the C++ command path. Escape and lost capture cancel gestures. |

Selection no longer seeks; the ruler and playback slider control the committed playhead. Skimming starts disabled and never changes Split or edit command targets. Main-sequence moves insert at existing boundaries. Narration retains its absolute offset during ripple edits and music continues looping to the project end.

Current import/open/save/save-as, quarter-turn rotation, splitting and hard-cut guards, recent music downloads/Mixkit, narration offset, −60 to +12 dB music gain, shared crossfades, proxies, native resolution, export progress, dirty-state prompts, and undo/redo remain available. Export retains its existing blocking workflow; the later recovery, compositing, caption, animation, and per-cut-transition roadmap is separate work.

## Implementation boundaries

- [`qml/Main.qml`](../qml/Main.qml), [`qml/Inspector.qml`](../qml/Inspector.qml), and [`qml/Button.qml`](../qml/Button.qml) provide the shell, menus, panels, controls, keyboard routes, and theme bindings.
- [`src/editor_controller.cpp`](../src/editor_controller.cpp) owns the authoritative project, selection, validation, undo, native dialogs, proxy replacement, playback intent, and export.
- [`src/quick_timeline.cpp`](../src/quick_timeline.cpp) is a Qt Quick painted item for timeline geometry and transient gestures. It paints cached visible media; it does not decode media or commit edits while painting.
- [`src/media_cache.cpp`](../src/media_cache.cpp) prepares thumbnails and waveform peaks asynchronously with one extraction process. The caches allow 16 MiB of images and 8 MiB of peaks; the background queue is capped at 128 jobs, with at most two queued urgent comparison images beside the active job.
- [`src/preview.cpp`](../src/preview.cpp) preserves the synchronous compatibility API and adds one coalescing still-decode worker with guarded GUI delivery. Producer replacement waits for owned decode work before releasing MLT resources.
- [`src/preview_item.cpp`](../src/preview_item.cpp) displays frames directly in Qt Quick without moving image bytes through JavaScript.

The migration also incorporates the project’s newer preview-range preparation, playhead-priority scheduling, hardware H.264 export with software fallback, and preview preparation pause/resume during export. Partial preview copies apply their source-frame offset only in the preview project; saved edits and export retain original source times.

The legacy Widgets window remains only in regression targets. Qt Widgets also supplies native file/message dialogs. The production entry point creates a QML application window. The timeline and preview currently upload painted images to Qt Quick; this migration does not claim hardware decoding or a zero-copy video pipeline.

## Verification

Verification was performed on arm64 macOS 15.7.3 with Qt 6.12.0 in a Release build. All 14 CTest executables passed, including the nine original regression targets and five new controller/cache/timeline/async-preview/QML targets. The new cases exercise real source files and MLT/FFmpeg operations, including source orientation, frame cadence, waveform amplitude, cancellation, stale results, group ordering, original-format compatibility, save/reopen, and export.

QML window tests exercise direct mouse and keyboard events for selection, trim commit/cancel, two-up images ready before release, transport Escape and lost-grab cancellation, playback restoration, list scrolling, numeric cancellation, focus transfer, text-entry shortcut isolation, and file drops. Layout tests keep all timeline lanes reachable at 760×560 with 24 pt text. All 13 QML test entries passed with software rendering, including screenshot capture in light/dark appearances and large text. Native Cocoa window creation, rendering, and pointer checks also passed. Native shortcut-focus automation could not be repeated reliably because macOS did not consistently activate the background test windows. The independent async/profile tests additionally passed AddressSanitizer runs.

The fresh review found and prompted fixes for priority comparison scheduling, keyboard list visibility, slider cancellation, inspector focus, numeric cancellation, and the original +12 dB music range. Regression tests cover those fixes.

### Warm seek measurement

A five-minute project containing 30 cuts from one synthetic 10-second 3840×2160 H.264/60 fps source was previewed through a prepared 960×540 H.264 proxy on local temporary storage. After 10 warmups, 100 distributed seeks measured:

| Measurement | Latency |
| --- | --- |
| Mean | 4.42 ms |
| 95th percentile | 4.79 ms |
| Maximum | 4.89 ms |

These measure the asynchronous decode-to-delivery path in that synthetic workload. They do not establish end-to-display latency, 60 Hz presentation pacing, varied camera-codec performance, cold-source behavior, or 200 distinct-source memory usage. Separate worker tests prove a GUI heartbeat continues while decode is blocked and that bursts retain only the newest request.

### Remaining device checks

Live VoiceOver, physical trackpad pinch, native shortcut focus, and Finder interactions still need hands-on device checks. Automated accessible-name, keyboard, and synthetic drop/gesture tests do not replace those checks. Windows and Linux builds remain unverified. A representative mixed-camera performance run and a 200-source memory profile remain release checks.

## Screenshots

[Dark interface](editor-qml-dark.png), [light interface](editor-qml-light.png), and [large-text workspace](editor-qml-large-text.png) show the migrated editor using test footage and a generated audio signal. Short or large-text workspaces scroll to retain editing controls and all lanes.
