# Clip Landing Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show exactly where a dragged timeline clip or group will land.

**Architecture:** Derive the landing bounds from the existing transient project
and preview order, then paint all guidance from those bounds. Commit through
the existing group-move signal and controller.

**Tech Stack:** C++17, Qt Quick painted item, Qt Test.

**Spec:** `docs/superpowers/specs/2026-10-27-clip-landing-preview-design.md`

## Global Constraints

- Add no dependencies.
- Preserve existing reorder, transition, selection, cancel, and undo semantics.
- Retain the existing staged changes and leave the Git index untouched.

## Review Focus

- Forward moves must account for removal of the dragged clip from its old slot.
- Crossfades and unequal clip lengths must use effective preview geometry.
- Groups must highlight every dragged clip and preserve relative order.
- Labels must remain visible at viewport edges and with large fonts.
- Cancel/no-op and release-position changes must preserve existing behavior.

### Task 1: Paint landing guidance from committed-order geometry

**Files:**
- Modify: `src/quick_timeline.h`, `src/quick_timeline.cpp`
- Test: `tests/quick_timeline_test.cpp`

**Interfaces:**
- Consumes: `preview_`, `previewOrder_`, `group_`, `clipRect(int)`, and
  `groupMoveRequested(const QVariantList &, int)`.
- Produces: private landing bounds and placement text used by painting/status;
  no new public interface or project data.

- [x] Add regression tests for painted landing geometry, placement wording,
  preview/commit equality, groups, transitions, viewport edges, and cancel.
- [x] Build `quick_timeline_test` and run the new tests; confirm failures.
- [x] Add the smallest shared landing calculation and paint the translucent
  outline, insertion line, and bounded label; reuse placement text in status.
- [x] Run all timeline tests and related controller/QML tests; build the app.
- [x] Inspect light/dark and large-font renders and request a focused review.
- [x] Record verification, retaining changes in the workspace for the user.

## Verification results

- App and affected test targets build successfully in `build/`.
- All 56 timeline checks pass. New regression checks were observed failing
  before the guidance implementation, fractional font-width correction, and
  duration-changing viewport correction, respectively.
- Full `qml_shell_test` passes. Controller selection/history, trim/split/reopen,
  and no-op selection/audio timing checks pass.
- Light, dark, and 24pt rendered labels were inspected. The large-text test
  compares the rendered glyphs with the complete placement phrase.
- Independent review found unnecessary large-font truncation and drop-time
  refitting when global fades change duration. Both were corrected, with
  failing-then-passing coverage including duration growth/shrinkage under fit
  and manual zoom/scroll.
- The separate `preparesVideoOverlayPreviewWithoutChangingSavedMedia` test
  times out waiting for a preview cache copy in both the older `build-append`
  executable and the current build. No cache implementation was changed.
- No commits, index changes, or unrelated source edits were made for this task.
