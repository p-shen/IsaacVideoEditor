# Transition Range Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Select clip endpoints, configure their enclosed transitions without editing the project, and apply the range as one undoable operation.

**Architecture:** Keep transition fitting in Project and atomic editing in EditorController. Stage range settings in the existing QML inspector, and pass the active range to QuickTimeline for highlighting. Preserve the existing immediate single-cut controls.

**Tech Stack:** C++17, Qt 6 Quick/QML, MLT 7; no new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-01-transition-range-editing-design.md`

## Global Constraints

- The earliest and latest selected clips define the range, including intervening clips.
- Preserve outside transition types and effective overlap durations.
- Stage range edits until Apply; one commit and one Undo entry.
- Preserve 60 fps timing, original media, existing effects, preview/export parity, and older projects.
- Work in a separate managed checkout; leave the current performance work untouched.

## Review Focus

- Sparse/reversed endpoint selection must resolve to the same enclosed cuts.
- A short endpoint clip must not shorten or lengthen an outside transition.
- Focused fields and delayed Apply events must not reuse drafts after target/project changes.
- Mixed types/durations must require concrete choices; Cut must work without duration.
- Compact layouts, keyboard cancellation, and ordinary move/delete selection must retain their behavior.

### Task 1: Atomic model and controller range editing

**Files:** `src/project.h`, `src/project.cpp`, `src/editor_controller.h`, `src/editor_controller.cpp`, `tests/audio_transition_test.cpp`.

**Interfaces:**
- `bool Project::setTransitionRange(int firstClip, int lastClip, const QString &type, int frames)` fits the complete range together and returns whether the project changed.
- `QVariantMap EditorController::transitionRange() const`, with first/last indices, endpoint names, cut count, mixed type/duration data, and a revision-sensitive `key`; notify through `transitionRangeChanged`.
- `selectedTransition == 0` represents the explicit selected-range target; positive values retain individual cuts, and -1 remains no cut. `selectTransition(0)` validates range availability.
- `Q_INVOKABLE bool setTransitionRange(const QString &key, const QString &type, int frames)` rejects stale targets, validates once, commits once, and reports applied/shortened counts with Undo.

- [x] Add failing controller tests using runtime invocation so absent APIs produce a test failure rather than a compiler failure. Select indices 1 and 4 in six clips, apply 18-frame Wipe Left, and assert cuts 2/3/4 change while cuts 1/5 retain their original settings. Assert a single Undo restores all cuts and Redo reapplies them.
- [x] Build `audio_transition_test` and run the new cases; expect missing range support to fail.
- [x] Implement the APIs using a project copy and one controller commit. Fit all requested cuts together using the existing proportional playable-media rules, reserving outside overlap first. Preserve observable outside durations when materializing inherited/constrained timing.
- [x] Cover reversed selections; short interior and endpoint clips; legacy global fades; invalid type/duration/range; Cut; no-op Apply; stale keys after selection changes, reorder, load, and Undo; and save/load.
- [x] Run `ctest --test-dir build -R '^(audio_transition_test|project_test|editor_controller_test)$' --output-on-failure`; expect all selected targets to pass. Commit only scoped files.

### Task 2: Staged GUI workflow and timeline scope

**Files:** `qml/CutInspector.qml`, `qml/Inspector.qml`, `qml/Main.qml`, `src/quick_timeline.h`, `src/quick_timeline.cpp`, `tests/inspector_ux_test.cpp`, `docs/advanced-editing.md`.

**Interfaces:**
- Consume `transitionRange`, `selectedTransition == 0`, and `setTransitionRange(key,type,frames)` from Task 1.
- `QuickTimeline::transitionRange` is a QVariantMap with first/last indices; changing it invalidates painting only, not clip selection.
- The shared Transitions action selects target 0 and opens inspector section 2. The inspector's target chooser includes Selected range plus existing single cuts.
- Range controls retain `draftKey`, `draftType`, and `draftFrames`; Mixed values remain unset until configured. The Apply handler checks the draft key and passes it unchanged to the controller.

- [x] Add failing UI tests for the toolbar action, visible range count, staging without dirty/Undo changes, explicit Apply, Mixed fields, Cut, Escape, stale focused duration, and switching between range/single targets. Expect missing controls or staging to fail before implementation.
- [x] Add the timeline/Clip-menu action; staged range controls in the existing inspector; endpoint filenames and replacement scope; and Apply to N cuts. Reuse existing fields and preset catalog. Keep single-cut controls and all-cuts action separate.
- [x] Reset staged values on selection/project/target changes and Escape. Clear the range highlight when leaving its inspector section. Paint a span indication and affected-cut outlines without changing clip selection.
- [x] Run `ctest --test-dir build -R '^(inspector_ux_test|audio_transition_test|qml_shell_test|quick_timeline_test)$' --output-on-failure`; expect all available selected targets to pass. Update editing documentation.
- [x] Build the application and run the full CTest suite. Exercise the actual app using a synthetic project, keyboard selection/configuration/Apply/cancellation, sparse endpoints, a narrow window, and large text. Inspect synthetic playback/export if fitting changes timing.
- [x] Request a focused independent code review of this feature, resolve important findings with regression coverage, and rerun affected checks. Commit scoped changes and report verified behavior and platform limits.


## Verification results

- Full application and test build succeeded on Apple silicon macOS, Qt 6.12 / MLT 7.42.
- Full CTest suite: **31/31 passed**, 177.30 seconds.
- Native Cocoa GUI run: **8 passed**, including setup/cleanup, sparse endpoint selection, staged Apply, Mixed values, keyboard editing, focused-frame mouse Apply with both focus policies, Escape from the target chooser, and 760×560 layout with 24-point text. Light/dark/large-text screenshots were inspected.
- Synthetic range export: 306 decoded video frames, retained AAC audio, blended pixels at all three enclosed cuts, and hard cuts at both outside boundaries.
- Independent review found excessive materialization of distant duration requests. A failing regression now passes with materialization limited to cuts needed to preserve timing; distant requested values survive later extension.
- Additional failing regressions exposed focused frame input not being staged by mouse Apply and Escape depending on focused control. Both pass after fixes. No deferred review findings.
- Range fitting retains the existing model's materialization of inherited legacy values. At a constrained outside boundary, preserving current playback duration can require storing its current playable length. This avoids changing outside playback; distant explicit requests remain intact.
- The existing Inspector routing required no changes to `qml/Inspector.qml`. No new dependencies or project schema.
- Linux and Windows were not run. Implementation remains in its managed feature checkout; the ongoing performance checkout was left untouched.
