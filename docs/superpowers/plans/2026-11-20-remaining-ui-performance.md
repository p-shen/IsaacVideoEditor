# Remaining UI Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Implement all four approved remaining editor responsiveness improvements.

**Architecture:** Cached row notifications and coalesced previews, one recovery worker, acknowledged producer ownership handoffs, and immutable time indexes.

**Tech Stack:** C++17, Qt Quick/Qt Test, MLT 7, existing dependencies.

**Spec:** `docs/superpowers/specs/2026-11-20-remaining-ui-performance-design.md`

## Global Constraints

- Preserve 60 fps timing, source offsets, rendering order, undo/cancel, persistence and original media.
- Preserve user changes and the previous timing patch; leave changes uncommitted.
- Retain bounded work queues and producer/profile/cache lease lifetimes through worker acknowledgment.

## Review Focus

- Rapid edits to several cues retain full and per-cue updates; cancellation never reapplies a timer's old text.
- Continuous edits still yield recovery snapshots; clear and newer flushes cannot be overwritten by staged old work.
- Superseded producer handoffs and canceled seeks release ownership only after native work finishes.
- Half-open boundaries, containing intervals, overlaps and backward seeks retain stable composition order.
- QML row selection, keyboard edits, warnings and geometry remain correct after undo, import and load.

### Task 1: Caption records and preview coalescing

**Files:** `src/caption_model.{h,cpp}`, controller/caption-controller sources, `qml/CaptionInspector.qml`, caption model/controller/QML tests.

- [x] Add row-update/warning regressions; observe missing-model failure.
- [x] Implement stable records and cached warnings with per-row transient updates.
- [x] Route QML through the model and cheap count/review properties; combine typing previews.
- [x] Verify integrated caption/model/QML tests, including final commit/cancel/undo and pending edits across cue changes.

### Task 2: Background recovery

**Files:** `src/recovery_store.{h,cpp}`, project serialization helper, `tests/recovery_store_test.cpp`.

- [x] Reproduce synchronous timer blocking and publication starvation in focused tests.
- [x] Implement one worker, shared serialization, coalescing, generation invalidation and draining compatibility.
- [x] Run isolated recovery tests.
- [x] Verify integrated recovery and export tests.

### Task 3: Decoder handoffs

**Files:** `src/preview.{h,cpp}`, editing controller call sites, preview/transport/controller tests.

- [x] Reproduce interactive seek blocking while native playback decode is held.
- [x] Add async play/pause/producer requests and bounded keyed edits, with ownership acknowledgment.
- [x] Integrate timeline/source retirement and preparation callbacks with revision guards.
- [x] Verify pending edits, cancellation, blocked decoders, ownership and controller flows in the integrated suite.

### Task 4: Active/visible content indexes

**Files:** `src/time_interval_index.h`, text/visual renderers, timeline sources, render/timeline tests.

- [x] Add interval equivalence, queued-frame, active-render and viewport regressions.
- [x] Implement active snapshots, one-cue updates, prior-active still cleanup and indexed viewport queries.
- [x] Run isolated renderer checks and large sparse lookup measurements.
- [x] Run integrated pixel/composition/timeline checks.

### Task 5: Complete verification

- [x] Build all targets and run the full CTest suite.
- [x] Check native QML keyboard/edit/cancel and timeline flows using disposable synthetic data.
- [x] Obtain independent review of the task diff and fix material findings.
- [x] Record measured performance, platform limits, and update the normal development app.

## Execution notes

The existing `build/` generation was progressing unusually slowly. Its exact task-owned CMake process was stopped after identification; a fresh Makefiles build in `/tmp/isaac-remaining-performance/build-make` configured/generated in 5.2 seconds. The failed Ninja configure installed nothing. Verification uses the fresh build to avoid stale mixed UI/core libraries.

## Results

- Final fresh Release build succeeded; all 36 CTest targets passed in 205.65 seconds on macOS arm64.
- Application smoke passed QML/SVG/FFmpeg, media decoding, playback, and MP4 export.
- Native UI checked with a disposable 50-clip/500-caption project: navigation, edit cancellation, commit, undo/redo, zoom/fit, and play/pause passed.
- Caption-model tests pin single-row changes and cached warning refresh. Recovery tests cover a large snapshot, coalescing, sustained-edit publication, clear/flush ordering, retry and destruction.
- Preview regressions cover blocked native decoding, ownership and superseded handoffs, full/keyed edits, stale errors, and reentrant frame callbacks.
- Narrow Inspector checks exposed an unrelated-notification effect of async preparation: readiness emitted selectionChanged and switched Inspector back to Clips. A separate clip-detail notifier and compact-mode regression repair it; the regression waits for compact layout before importing.
- Independent task review and final delta review found no actionable issues. User edits and the earlier timing patch remain intact; no commits were made.
- The normal development app was updated with atomic file replacement; its executable hash matches the tested app. The prior app is retained at build/.IsaacVideoEditor-before-ui-performance.app. No running editor was restarted.
- Windows/Linux and the user’s actual long project remain unverified. Explicit flush and shutdown still drain in-flight work by design.
