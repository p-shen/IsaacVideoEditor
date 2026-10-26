# IsaacVideoEditor universal design

The editor uses a Qt Quick/QML interface across desktop platforms. It keeps the system font and follows the system light or dark color scheme. The video canvas stays dark so footage has a stable surround.

## Principles

- **Perceivable:** body text targets at least 4.5:1 contrast; meaningful control borders and focus states target at least 3:1. Selection, disabled state, progress, and errors use text or shape as well as color.
- **Operable:** every drag action has a button or numeric path. Tab reaches native controls; both the video slider and timeline support arrows (one frame) and Page keys (one second). Space plays or pauses. Undo/Redo restore edits, and View offers focus shortcuts for Clips, Timeline, and Inspector.
- **Adaptable:** the workspace uses resizable browser, viewer, inspector, and timeline panels. In narrow windows Inspector moves into a tab beside Clips. With large text, short windows scroll the workspace; individual inspector pages also scroll. Form rows and preview messages wrap.
- **Consistent:** a neutral tool workspace uses the same labels, coral accent, and interaction states in both themes. Qt Quick Controls and a shared palette provide predictable controls on macOS, Linux, and Windows.

Theme colors and interaction states live in [`src/theme.cpp`](../src/theme.cpp). The workspace lives in [`qml/Main.qml`](../qml/Main.qml), the fields in [`qml/Inspector.qml`](../qml/Inspector.qml), and the interactive timeline in [`src/quick_timeline.cpp`](../src/quick_timeline.cpp). The Qt Quick clip list, playback slider, range-selection dialog, buttons, and numeric inspector fields provide accessible alternatives to the painted timeline. The editing controller owns committed state and undo; canceled gestures never become edits.

## Checks

The workspace is visually checked at 1320×860 and 760×560 with normal and 24 pt text in light and dark themes. Automated tests cover project order and versions, file drops, exact frame seeking, source orientation, waveform extraction and display scale, trim bounds and snapping, group selection/reorder, gesture cancellation, playhead preservation, stale asynchronous results, Undo/Redo, text-entry shortcut isolation, focus transfer, clip-list visibility, short/large-text layouts, and theme switching. Live screen-reader use and builds on Windows and Linux remain to be checked on those systems.

Normal text contrast is at least 4.5:1 in both palettes. Measured dark/light body ratios are 14.61/15.14, muted text 8.55/7.38, control boundaries 4.02/3.88, and focus 9.34/6.53.

QML replaces the prior native Widgets controls, so live VoiceOver remains a required release check. Automated checks verify the playback slider's accessible interface/name and keyboard alternatives; they do not establish live screen-reader usability. The workspace scrolls vertically when a short window or large font cannot fit all timeline lanes.
