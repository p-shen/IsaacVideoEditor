# Crossfade UX Implementation Plan

> **For agentic workers:** Execute inline with superpowers:executing-plans and test-driven development. Preserve the shared checkout's existing work.

**Goal:** Apply crossfades to all existing cuts with local shortening, useful feedback and contextual Undo.
**Architecture:** Reuse persisted per-cut requests and the existing overlap bounds. Keep legacy inherited projects unchanged until explicit Apply. Reuse the notice banner for results and Undo.
**Tech Stack:** C++17, Qt Quick/QML, MLT, QtTest.
**Spec:** The three UX recommendations accepted by the user in this chat.

## Global Constraints
- Apply changes current boundaries, including appended clips and split points. Later cuts still start with their own settings.
- Never retime an old project merely by opening it.
- Zero removes fades; one Undo restores the previous edit.
- Preserve existing unrelated changes; do not commit the shared working tree.

## Review Focus
- One-frame clips must not suppress distant fades.
- Undo links must disappear after another edit or notice replaces their result.
- Reapplying unchanged settings must not create extra history.
- Saved and reopened requests must export the same local fades.
- Shortened cuts must show actual duration, including zero overlap.

### Task 1: Local fade application and export
**Files:** src/editor_controller.cpp, tests/audio_transition_test.cpp.
**Interfaces:** setCrossfade(double), Project::transitionFramesBefore(int), saved transitionFrames.
- [x] Extend export regression with a short middle clip; add count/duration and one-frame cases. Run and observe failure.
- [x] Store explicit per-cut requests on Apply, using the existing local overlap calculation.
- [x] Verify nearby shortening, distant fades, save/reopen, actual exported pixels and zero/Undo.

### Task 2: Feedback, Undo and scope
**Files:** src/editor_controller.h/.cpp, qml/Main.qml, qml/CutInspector.qml, tests/inspector_ux_test.cpp, docs/advanced-editing.md.
**Interfaces:** noticeCanUndo property; currentTransition.shortened; existing notice and undo commands.
- [x] Add failing behavior tests for applied counts, shortening feedback, contextual Undo, replacement/dismissal and new cuts after Apply.
- [x] Add result feedback and a scoped Undo button. Name the action Apply to all existing cuts and explain reapplying after new cuts.
- [x] Show requested and effective lengths for shortened selected cuts.
- [x] Build, run focused checks, visually inspect normal and narrow layouts, run the full suite with normal cache access, and request a focused read-only review.

## Verification

- Short-clip, one-frame, scoped Undo, no-op, future-import and legacy timing regressions passed.
- Actual saved/reopened MP4 exports passed decoded frame counts and all three cut-pixel checks in both duration cases.
- Dark/light and 760×560 at 24 pt inspected; scoped Undo and zero-length shortening note verified.
- Focused review found the alternate Import and append path inherited the previous duration; a failing regression preceded the hard-cut default fix. No remaining review findings.
- Final full CTest suite: 28/28 passed in 121.07 seconds with normal preview-cache access.
