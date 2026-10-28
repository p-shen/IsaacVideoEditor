# Inspector Simplification Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans with independent panel tasks dispatched in parallel. Record verification here; preserve the user's active checkout and unrelated working changes.

**Goal:** Implement the approved right-panel audit without removing editing capabilities.

**Architecture:** Retain the six inspector modes and controller APIs. Share a small expandable-group component and integrated numeric steppers. Individual panels own their groups; the shell owns section navigation and scrolling.

**Tech Stack:** C++17, Qt 6 Quick/Controls Basic, QML, existing CMake/QtTest tests.

**Spec:** `docs/superpowers/specs/2026-10-27-inspector-simplification-design.md`

## Global constraints
- No new dependencies or file-format changes; preserve unrelated concurrent work.
- Use current theme tokens, accessible names, keyboard focus and Escape cancellation.
- Expandable content remains instantiated, so bindings and existing object names survive collapse.
- `UI.InspectorDetails`: `title: string`, `summary: string`, `expanded: bool = false`, default content in a ColumnLayout. Header objectName is `<objectName>Toggle`.
- `UI.TextField`: optional `stepper: bool = false`, `stepRequested(int direction)`; Up/Down and inline plus/minus request a step without formatting or committing another value themselves.

## Review focus
- Changing source/selection while a numeric field has focus must not commit to a different object.
- Hidden groups must not steal keyboard focus; active warnings and pending canvas Apply/Cancel remain discoverable.
- Seconds/percent conversion must round once to the existing frame/value model and preserve Escape bindings.
- Narrow windows and 24 pt text must retain reachable controls and avoid horizontal overflow.
- Switching to a short/empty panel must not inherit another panel's scroll offset.

### Task 1: Shared controls and inspector shell
Files: `qml/InspectorDetails.qml`, `qml/TextField.qml`, `qml/Inspector.qml`, `qml/qmldir`, `qml/qml.qrc`, `tests/inspector_ux_test.cpp`, `CMakeLists.txt`.
- [x] Add and run failing interaction tests for independent scroll positions and keyboard frame stepping.
- [x] Add shared controls; simplify Clip and replace shell Audio/Transitions with panel components.
- [x] Verify first-entry scroll, restoration, compact sizing and Escape behavior.

### Task 2: Audio and Transitions
Files: `qml/AudioMixer.qml`, `qml/CutInspector.qml`.
- [x] Add and run failing source-scope and no-cut preset checks in the shared inspector test.
- [x] Implement one-source Audio with primary controls and secondary groups; move global cut application to an explicit bulk action.
- [x] Verify real project edits, correct source selection, frame/second conversion and cancel/undo.

### Task 3: Text and Captions
Files: `qml/TextInspector.qml`, `qml/CaptionInspector.qml`, `qml/TextAttachment.qml`.
- [x] Add and run failing empty-state and collapsed-placement checks in the shared inspector test.
- [x] Implement compact text formatting, grouped placement/effects/attachment, and caption editing/timing with secondary style/file actions.
- [x] Verify existing editing transactions, style targeting, navigation and Escape tests, expanding groups where needed.

### Task 4: Transform and Canvas
Files: `qml/CompositionInspector.qml`, `qml/Main.qml`.
- [x] Add and run failing default-animation-density and custom-canvas checks.
- [x] Implement basic transforms, percent-based precision, optional animation and canvas setup with visible pending actions.
- [x] Verify exact controller updates, animation operations and preview cancellation.

### Task 5: Integration and delivery
Files: tests, user-facing documentation and relevant screenshots.
- [x] Run the full build and CTest suite; inspect light/dark, normal/narrow/large-text screenshots.
- [x] Request an independent whole-change review and fix important findings with regression coverage.
- [x] Rebuild the app and report completed behavior and remaining verification limits.

## Progress
- Setup: approved audit recorded; implementation authorized. Work proceeds in the existing checkout with a temporary snapshot of touched files. Unrelated QML-shell test edits and update-checker work were present before this task and must be preserved. No branch, commit, or release operation is part of this request.
- Tasks 1–4: implemented. The first regression run reproduced seven failures; all are corrected. Each mode owns a ScrollView rather than manually maintaining offsets. Shared details groups and steppers preserve instantiated bindings and keyboard access.
- Independent review: found focus-only position writes that could round text placement or copy it across selected captions. Corrected with a failing-then-passing regression that also covers mixed caption sizes. A rounded maximum fade was reclassified as an important valid-input issue and corrected with a 61-frame boundary regression.
- Additional QA: reproduced and corrected stale clip edits targeting another selection. Tightened large-text checks to use live visual items, complete control bounds, window bounds and panel bounds. This exposed compact inspector minimum-height overflow beneath the toolbar; the new regression failed, then passed after bounding the compact stack/loader.
- Review decision: the dissolve preset intentionally requires a selected cut for both click and drag, matching the approved no-selection guard. Cost: one explicit cut selection before dragging a preset. No second drag-only control was added to the simplified panel.
- Review scope: concurrent updater/release changes remain untouched. Export/model behavior is covered by the full suite; native dialogs, VoiceOver and other operating systems are not newly verified by this UI change.
- Verification so far: inspector suite 17/17, existing shell 41/41, first complete CTest run 26/26. Final build succeeded after the compact-boundary correction; the complete workspace suite passed 26/26 in 37.73 seconds. Source-QML inspector checks passed 17/17 with no warnings. Normal light/dark and strict 760×560 / 24 pt captures are saved in `docs/inspector-ui/`.
