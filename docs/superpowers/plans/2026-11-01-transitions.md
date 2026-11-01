# Transition Presets Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 12 transition presets that work consistently in editing, playback, saved projects, and export.

**Architecture:** Share stable preset IDs/display names across model, controller, inspector, and timeline. Configure the existing MLT luma transition with generated, cached PGM masks; retain the separate audio mix.

**Tech Stack:** C++17, Qt 6, QML, MLT 7, FFmpeg.

**Spec:** `docs/superpowers/specs/2026-11-01-transitions-design.md`

## Global Constraints

- No new dependencies, shader pipeline, or packaged plugins.
- Preserve legacy projects, existing audio/overlap timing, and unrelated working changes.
- Preserve the half-second Dissolve drag preset and bulk crossfade behavior.

## Review Focus

- Changing effect without changing overlap must create a real undoable edit.
- Duration fields and timeline drags must retain the selected effect, including cancellation.
- Reverse-direction and circle-close effects must still run from outgoing to incoming footage.
- Short clips, one-frame overlaps, mixed aspect ratios, and random seeks must render valid frames.
- Preview and MP4 export must use the same effect and retain audio.

### Task 1: Model and rendering

**Files:** new `src/transition_presets.h/.cpp`, `tests/transition_test.cpp`; modify `src/project.cpp`, `src/mlt_engine.cpp`, `src/editor_controller.h/.cpp`, `CMakeLists.txt`.

**Interfaces:** `Transitions::presets()`, `Transitions::find(const QString &)`, `Transitions::name(const QString &)`, and `Transitions::configure(Mlt::Transition &, const QString &, const QSize &, QString *)`; controller exposes constant `transitionPresets` and preserves the actual preset ID in `currentTransition`.

- [x] Write tests for all 12 IDs: valid save/load, type-only changes, invalid IDs, undo/redo, short clips, spatial pixel expectations, and MP4 parity/audio.
- [x] Build/run tests and confirm failures reflect missing presets.
- [x] Implement the shared catalog, mask generation, validation, change detection, and MLT configuration.
- [x] Run the focused transition tests and inspect rendered images.

### Task 2: Editing interactions and delivery

**Files:** `qml/CutInspector.qml`, `src/quick_timeline.cpp`, controller's timeline connection, `tests/inspector_ux_test.cpp`, `tests/audio_transition_test.cpp`, `docs/advanced-editing.md`.

**Interfaces:** consume the catalog and `currentTransition.type`; retain the existing transition-preview signal and Dissolve drag/drop format.

- [x] Add failing inspector and timeline gesture tests for effect selection, duration preservation, undo, and cancellation.
- [x] Populate the effect chooser from the catalog; preserve effect IDs on duration edits and overlap dragging.
- [x] Build the app and run focused tests, then the complete CTest suite.
- [x] Inspect the inspector and transition renders; request a final code review, address important findings, and document results.

## Implementation notes

The timeline preview signal now includes the effect ID so dragging an overlap cannot silently substitute Dissolve. Mask maps are generated once per effect/aspect ratio in a private temporary directory, then reused; MLT uses the same configuration for preview and export. No packaged assets or new MLT modules are required.

Independent review identified a zero-playable-overlap case that hid a stored mask behind Cut in the inspector. A failing-then-passing regression now covers selecting a mask, trimming to a one-frame clip, editing duration, and restoring clip length. The controller preserves the stored mask while retaining legacy zero-duration Dissolve presentation. Unrelated existing text/caption changes were excluded from the review and preserved.

## Verification results

- Full rebuild succeeded; `ctest --test-dir build --output-on-failure` passed 31/31 targets in 185.45 seconds with normal user-cache access.
- The 41 new transition cases cover every preset's save/load, spatial preview pixels, decoded MP4 frame count, exported audio, out-of-order seeking, and one-frame overlaps on a portrait canvas.
- Inspector and timeline regressions cover keyboard selection, duration edits retaining effects, undo/redo, resize/cancel, and zero-playable-overlap recovery. The independent review's single Important finding was reproduced failing and then passing; no other scoped findings remained.
- Inspected all 12 rendered masks and inspector screenshots in light/dark themes, the open chooser, and a 1000×660 window with 24-point text. No new dependencies or packaged modules.
- Verified on Apple silicon macOS, Qt 6.12 and MLT 7.42. Other platforms were not run.
