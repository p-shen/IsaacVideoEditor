# Text Overlays Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. The user has already authorized execution.

**Goal:** Add editable, timed titles and labels to the migrated editor and exported video.

**Architecture:** Project-owned frame-timed overlays, existing undo snapshots and commands, a shared Qt/MLT compositor, QML text inspector, and native Qt Quick viewer/timeline gestures. Work in an isolated copy of the delivered migration and merge only changed files into the current project, preserving concurrent styling.

**Tech Stack:** C++17, Qt 6 Quick/QML/Gui, MLT 7, CMake, QtTest.

**Spec:** `docs/superpowers/specs/2026-10-26-text-overlays-design.md`

## Global Constraints

- Existing 60 fps timing, proxies, original-source export and project versions 1–4 remain supported.
- Preview and export share layout, style, timing and fades.
- Numeric font size; no redundant text-start field or playback-position slider.
- Preserve video editing behavior and concurrent styling work.

## Review Focus

- Malformed/non-finite JSON fields must fail without changing a loaded project.
- Multiple overlapping titles must retain order, selectability and independent timing.
- Undo/cancel must restore one whole drag, including selection and timing.
- Proxy-size preview and original-size export must preserve normalized text position and scale.
- Deleting all text must leave Add text working; video trims must not destroy saved overlay data.

### Task 1: Project persistence and shared renderer

Files: `src/project.h/.cpp`, new `src/text_renderer.h/.cpp`, `src/mlt_engine.cpp`, `CMakeLists.txt`, `tests/text_overlay_test.cpp`.

- [x] Add a roundtrip test for version-5 JSON text overlays and watch loading fail before implementation.
- [x] Add `TextOverlay` with text/font/style/color/bold/shadow/fade, frame timing and normalized position; validate, save and load with old-version compatibility.
- [x] Implement `TextRendering::bounds`, `paint`, `attach`, and `update` using one Qt layout/compositor attached to the MLT tractor.
- [x] Verify real MLT frames before/during/after overlays, fades, layering, scaled layout and an exported MP4.

### Task 2: Controller commands and history

Files: `src/editor_controller.h/.cpp`, `tests/editor_controller_test.cpp`.

- [x] Test adding at the playhead, property edits, deletion, persistence, and undo/redo through the public QML interface; observe missing behavior first.
- [x] Expose `textOverlays`, `selectedText`, `currentText`, `fontFamilies`; add/select/delete/update/timing/drag commands.
- [x] Extend snapshots and equality; update only the text compositor for text-only edits. One viewer drag has one undo step and Escape cancellation.
- [x] Verify empty text state and invalid input; preserve clip selection semantics and media behavior.

### Task 3: Timeline, viewer and QML controls

Files: `src/quick_timeline.h/.cpp`, `src/preview_item.h/.cpp`, `qml/Main.qml`, new `qml/TextInspector.qml`, `qml/Inspector.qml`, `qml/qml.qrc`, existing timeline/shell tests.

- [x] Add real-event tests for overlay drag/trim/cancel and viewer move; observe missing behavior first.
- [x] Add stacked text lanes and frame-accurate bounds, text selection and move/edge-trim signals using existing timeline navigation.
- [x] Implement viewer hit testing/dragging against shared layout bounds, with keyboard movement and cancellation.
- [x] Add timeline Add text/Delete text, numeric/style/text inspector and text selection navigation; remove the separate position slider while retaining time display and ruler navigation.
- [x] Reconcile the latest concurrent styling before editing shared QML; verify light/dark and narrow/large-text layouts.

### Task 4: Review and delivery

- [x] Run the full CTest suite, inspect native UI and an actual text-containing export.
- [x] Request a fresh whole-change review; repair actionable findings and rerun affected checks.
- [x] Merge changed files without overwriting newer user/styling changes; clean build and verify delivered app.
- [x] Document behavior and actual validation. Delete the monitoring automation only after the feature is delivered and verified.

## Delivered verification

Delivered 25 feature files to `/Users/Peter/Documents/ChatGPT/IsaacVideoEditor`, after checking every baseline source hash for newer changes. Unrelated styling and existing backup files were preserved. A clean Release build passed all 15 CTest targets (27.86 s). Native Cocoa checks passed 6 entries, including an actual demonstration export. The rebuilt production app was reopened, the saved demonstration project loaded through its native Open dialog, and the opening title, text timeline blocks, Add/Delete text controls and Text inspector tab were visually confirmed. The demonstration media and screenshots are retained in this chat's `verified-text-overlays` visualization folder.
