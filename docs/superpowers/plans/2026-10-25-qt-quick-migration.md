# Qt Quick Migration Implementation Plan

> **For agentic workers:** Use the planning, TDD, and parallel-agent skills. Steps use checkboxes to record progress.

**Goal:** Ship the existing editor in Qt Quick/QML with filmstrips/waveforms, responsive preview/navigation, and precise snapping/selection/trim interactions.

**Architecture:** A C++ EditorController owns the project, commands, undo history, native dialogs, proxy integration, and preview. QML supplies the shell, panels, menus, and numeric alternatives. A C++ QuickTimeline draws only visible media and emits edit requests; an asynchronous MediaCache provides derived images/peaks. All saved edits retain the existing Project and MLT engine.

**Tech Stack:** C++17, Qt 6 Quick/QML/Quick Controls, existing Qt Widgets only for native dialogs and legacy regression tests, MLT, existing FFmpeg executable.

**Spec:** ../specs/2026-10-25-qt-quick-interaction-design.md, adoption and priorities 1–3 only.

## Global Constraints

- Preserve 60 fps, exclusive-out convention, project versions 1–4, rotation, crossfade/hard-cut semantics, narration's absolute offset, looping music, native-resolution original-source exports, proxy timing, undo/redo, save dialogs, and recent music downloads.
- One C++ authoritative project; temporary trim/reorder previews never mutate it until release. One committed gesture creates one undo command; canceled/no-op gestures create none.
- Skimming is initially off, silent, does not change the committed playhead, and never changes command targets.
- Reorders insert at existing boundaries only. Trim snap acquisition/release: 8/12 logical pixels, nearest frame target and playhead tie preference; N toggle and Option/Alt override.
- Preserve system light/dark themes, keyboard alternatives, minimum 760×560 layout, readable 24 pt text, source files, and save/export validation.
- Retain Widgets only as comparison/test code; the production entry point creates a QML ApplicationWindow.
- No Git metadata exists. Use the isolated /tmp/isaac-qml-work checkout, retain /tmp/isaac-qml-original, and copy verified changes back without deleting unrelated project files.

## Review Focus

- Stale or canceled seek/thumbnail callbacks must not overwrite a newer frame or revive canceled edits.
- Global crossfade clamping can change every boundary while trimming one short clip; temporary geometry and commit must agree.
- Noncontiguous selection must preserve group order when moved or deleted, including edge/no-op cases.
- Proxy replacement, source-bound trims, and split+rotate must agree with original-source export.
- QML text focus, minimum/large-text layouts, and native dialog cancellation must retain keyboard operation and dirty state.

## Files and interfaces

- Create src/editor_controller.{h,cpp}: QObject properties for project title/path/dirty, clips (QVariantList), selectedIndices, selectedClip, selected trim/audio fields, format/preview status, frame/position/playback, undo/redo/export state. Commands: importFiles(QStringList), selectClip(int,int modifiers=0), clearSelection(), moveSelection(int), moveGroup(QVariantList,int boundary), removeSelection(), trimClip(int,double in,double out), split(), rotate(int), seek(int), beginScrub(), finishScrub(bool cancel), skim(int), endSkim(), togglePlayback(), saveTo(QString), loadFrom(QString), new/open/save/saveAs/import/export dialog commands, audio setters and recent downloads.
- Create src/preview_item.{h,cpp}: QQuickPaintedItem drawing QImage frames from EditorController, plus CachedFrame drawing a MediaCache thumbnail for source path/time/rotation. No image pixels passed through JS.
- Create qml/Main.qml and qml/Inspector.qml: controls and accessible alternatives to all current actions.
- Modify main.cpp and CMakeLists.txt for QML resources, registrations, production targets, and new tests.
- MediaCache and QuickTimeline use the separate briefs below as their binding interfaces.

## Task 1 Controller and shell parity

- [x] Add tests/editor_controller_test.cpp first. Assert load/import/trim/split/rotation/reorder/audio/save/undo, no dirty on selection, invalid source bounds, and snapshot equivalence after reopen. Run and confirm failure before implementing.
- [x] Implement EditorController using Project and Media helpers. Keep native validation and dialogs; extract state from MainWindow instead of hiding a legacy window.
- [x] Implement QML controls for import/list/trim/start/length/rotation/audio/fades/playback/zoom/split/undo/save/export, adaptive layout, themes, and recent music downloads. Register QML types and package resources.
- [x] Verify controller tests and load the QML application offscreen with no component errors. Re-run original CTest regression targets.

## Task 2 Derived-media cache

- [x] Read .work/media-brief.md; write tests/media_cache_test.cpp first and run RED.
- [x] Implement src/media_cache.{h,cpp} asynchronous thumbnail/waveform generation, source fingerprinting, bounded memory/queue, retries and viewport priority, and playback throttling.
- [x] Verify actual fixture extraction, source/rotation mapping, absent audio, invalidation, cancellation, and cache bounds. Record RED/GREEN evidence in .work/media-report.md.

## Task 3 Timeline interaction

- [x] Read .work/timeline-brief.md; write tests/quick_timeline_test.cpp first and run RED.
- [x] Implement src/quick_timeline.{h,cpp} with visible media painting, waveform unavailable states, cursor-centered zoom/pan, skimming/scrubbing, drag-edge scroll, selection requests, transactions, group moves, precise trim previews, snapping/hysteresis, and existing fades/audio lanes.
- [x] Verify gestures via actual Quick window events and geometry assertions; record RED/GREEN evidence in .work/timeline-report.md.

## Task 4 Responsive preview and integration

- [x] Add a latest-request-wins async preview test, cancellation/playback restore test, and independent skim-position test; watch them fail.
- [x] Move MLT consumer and still rendering to an owned worker thread with coalesced seek requests and generation-safe delivery; keep public Preview API compatible with existing tests. Wire controller and timeline plus loading/trim-comparison views.
- [x] Add QML interaction smoke coverage for control focus, text-entry shortcut exclusion, cancel/no-op gestures, shell states, theme changes, and narrow/large-font rendering.
- [x] Run full CTest, capture QML screenshots in both themes and narrow/large-text layouts, exercise import/edit/save/export through real Qt events and file-based integration tests, and measure warm seek latency on the test Mac.

## Task 5 Delivery

- [x] Obtain a fresh review of the complete original-to-new diff, fix material issues with reproducing tests, and run the final suite.
- [x] Update README, universal-design notes, and implementation evidence to describe shipped behavior and any measured limits.
- [x] Copy verified source/docs/tests/resources and the built app back to the actual project, preserving unrelated files. Verify the copied content and launch the delivered application.

## Completion

Implemented October 26, 2026. All 14 test executables pass from a fresh build in the actual project folder. Native render/pointer checks and software-rendered QML interaction coverage are recorded in docs/qt-quick-migration.md. Physical device, live VoiceOver, native shortcut focus, and Windows/Linux release checks remain as documented. Concurrent preview/export improvements were merged and preserved.
