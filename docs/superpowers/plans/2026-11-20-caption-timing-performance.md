# Caption Timing Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Remove repeated caption timing scans and lane sorts from long-project editing.

**Architecture:** Caller-owned timing snapshots follow the existing TimelineData pattern. Resolve host positions once, reuse them in rendering/list construction, and retain timeline lane inputs until intervals change.

**Tech Stack:** C++17, Qt Quick/Qt Test, MLT 7; existing dependencies only.

**Spec:** `docs/superpowers/specs/2026-11-20-caption-timing-performance-design.md`

## Global Constraints

- Preserve 60 fps timing, original source/proxy offsets, undo, cancellation, persistence, and rendering order.
- Preserve all pre-existing working-tree changes; leave changes uncommitted.
- No recovery, decoder handoff, GPU, or project-format changes.

## Review Focus

- Attached cues with proxy offsets, trimmed hosts, reordered clips, and crossfades retain their start frames.
- Missing hosts use the established fallback; detached cues retain desired duration beyond the project end.
- Canceled gestures restore lane geometry, extent, and snap timing.
- Style-only edits and zooming retain the same lane topology; changed intervals recompute it.
- Titles remain independent from caption burn settings and caption-inclusive timeline extent.

### Task 1: Resolve text timing once

**Files:** `src/project.h`, `src/visual_model.cpp`, `tests/caption_test.cpp`.

**Interfaces:** Produce `Project::TextTiming { QVector<int> titleStarts, captionStarts; int duration, extent; }` and `TextTiming Project::textTiming() const`.

- [x] Add equivalence tests for attachment/fallback/offset and edited timelines; observe failure for the missing snapshot API.
- [x] Build one timing snapshot and use it in `renderedTexts()`.
- [x] Run `caption_test` and compare the existing synthetic rendering-data benchmark.

### Task 2: Reuse timing in timeline and caption summaries

**Files:** `src/quick_timeline.{h,cpp}`, `src/caption_controller.cpp`, `src/text_style.{h,cpp}`, `tests/quick_timeline_test.cpp`, `tests/caption_controller_test.cpp`.

**Interfaces:** Consume Task 1's snapshot; add a `timedTextInfo` overload that accepts it.

- [x] Add long-project interaction/performance and timing-summary equivalence regressions; run against the old implementation.
- [x] Retain committed/gesture text timing; use resolved starts for sorting, geometry, extent, and snap targets. Reassign lanes only when starts/durations change.
- [x] Build caption list records from one snapshot; retain existing notification and selection behavior.
- [x] Run affected timeline/controller/core checks, including keyboard changes and gesture cancellation.

### Task 3: Verify and review

**Files:** `docs/performance.md` and this plan.

- [x] Build the app and all tests, then run the full CTest suite.
- [x] Repeat the synthetic long-project benchmark and check the app with temporary media, including keyboard editing and Escape cancellation.
- [x] Have a fresh reviewer inspect the task diff against the captured pre-task baseline; fix material findings and rerun affected checks.
- [x] Document measured results and outstanding platform limits.

## Results

- Baseline focused tests: 3/3 passed. New long-timeline regression failed at 6,932 ms before the fix; after the fix it passed below the timer’s 1 ms resolution.
- Core snapshot and timing-summary tests first failed for their missing APIs, then passed with the implementation.
- Full Release build succeeded; all 34 CTest targets passed in 218.54 seconds.
- Median model measurement with 1,000 clips / 3,000 attached captions: preview text construction 163.2 → 0.278 ms; start resolution plus sorting 2,221 → 0.203 ms.
- Native app check used an isolated test application/project with 50 clips / 500 captions; keyboard navigation, Enter editing, Escape cancellation, and zoom/fit passed.
- Fresh independent review against the captured pre-task baseline found no actionable issues. Changes remain uncommitted; Windows/Linux and the user’s actual slow project are not profiled.
