# Remaining editing interactions implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Deliver ranks 4–7 and 9–10 while preserving the independently delivered text overlays and visual theme.

**Architecture:** C++ remains authoritative for frame arithmetic, data, validation and history. QML and QuickTimeline expose equivalent direct and numeric actions. Independent subsystems are developed against the same source snapshot in isolated copies; integration is sequential and each merge is reviewed and verified before delivery.

**Tech Stack:** Existing C++17, Qt 6 Quick/QML, MLT 7 and FFmpeg; no new dependencies.

**Spec:** `docs/superpowers/specs/2026-10-25-qt-quick-interaction-design.md`, sections 4–7, 9–10 and shared rules. Text comparison: `docs/remaining-interaction-roadmap.md`.

## Global constraints

- Keep frame arithmetic in C++; retain 60 fps and exclusive out.
- C++ owns committed project state; canceled gestures and unchanged edits create no history.
- Preserve source files, legacy versions 1–5, current text, theme, preview proxies and original-resolution export.
- Background export uses an immutable revision and atomic destination replacement.
- Every direct action has a numeric or button alternative; preserve both themes and narrow/large-text layouts.
- The source directory has no Git metadata. Use an isolated snapshot, record baseline hashes, and merge delivery only after checking concurrent modifications.

## Review focus

1. Missing media must retain all timing/edits and recovery must never overwrite the saved project.
2. Continuous audio gestures and canceled export must not leave transient edits or remove existing output.
3. Library ranges and insertion at overlapping cuts must use the committed project playhead.
4. Attachments and keyframes must survive split/trim/reorder/removal and undo with identical preview/export.
5. Two transitions on a short clip must clamp locally without negative duration or unintended changes at unrelated cuts.

### Task 1: Audio controls and individual transitions (ranks 4 and 10)

**Files:** `src/project.{h,cpp}`, `src/mlt_engine.{h,cpp}`, `src/editor_controller.{h,cpp}`, `src/quick_timeline.{h,cpp}`, `qml/Inspector.qml`, `qml/Main.qml`, new focused audio/transition tests and documentation.

**Interfaces:** Extend existing Clip/Project with backward-compatible audio controls and per-cut override data. Keep existing `transitionFramesBefore(index)`, `clipStartFrame(index)`, `durationFrames()` callers authoritative. Add focused controller commands and one audio gesture transaction. Meter/solo are preview state; mute/gain/fades are saved data. Integrator owns export cancellation and other added project fields.

- [x] Write and run failing tests for default legacy mix, gain/mute/fades, one-gesture undo/cancel, local overlap clamping, split hard cuts and project roundtrip.
- [x] Implement model/engine controls and timeline gain/fade/offset handles with numeric inspector parity; saved Loop to end, source audio controls, preview solo and clipping meter reset.
- [x] Add selectable per-cut transition type/duration/remove controls and drag duration preview; retain global convenience.
- [x] Verify relevant tests, rendered audio/frame behavior and full regression suite in the isolated branch. Record exact results and remaining checks.

### Task 2: Source browser (rank 5)

**Files:** project/controller/preview/timeline integration, new `qml/MediaBrowser.qml`, QML resources, source-browser tests.

**Interfaces:** A persisted reusable asset list separate from `clips`. A source cursor/range separate from `position`. Public import-only and explicit import/append, append-range and insert-range commands. Insert uses existing `Project::splitClip` restrictions. Legacy `importFiles` remains an append-compatible API for old callers; production import dialog becomes library import.

- [x] Prove import-only leaves duration unchanged; derive assets for legacy projects; roundtrip range and cursor; repeated independently trimmed uses; used/unused/search/audio filters; invalid/crossfade insert rejection.
- [x] Implement source preview and transport, I/O inclusive/exclusive mapping, Clear Range and explicit Append/Insert.
- [x] Implement media and timeline drop distinctions, insertion feedback, asset removal confirmation with usage count and no source deletion.
- [x] Verify integration with text and existing timeline tests; record results.

### Task 3: Connected visuals and reframing (ranks 7 and 9)

**Files:** project/controller/engine/preview/timeline integration, new focused compositor/transform helpers and QML inspector, composition tests.

**Interfaces:** Stable clip IDs and attachment to a host ID plus source frame; detached absolute time. Visual overlays are distinct from existing text. Per-instance transform and source-frame curves are shared by preview/export. `projectOutputSize` respects explicit even canvas dimensions, with legacy auto preserved.

- [x] Prove source-frame attachments through split/trim/reorder/delete/detach/undo, layer visibility/stacking, old project load and preview/export pixels.
- [x] Implement connected video/image add/move/trim lanes and saved visibility with numeric controls; host deletion includes attached overlays and offers detach first.
- [x] Implement landscape/portrait/square/custom canvas, fit/fill/crop, viewer handles/guides/reset and numeric alternatives.
- [x] Implement source-frame position/scale/rotation/opacity keyframes, previous/next/delete, linear/ease and split/trim curve preservation.
- [x] Verify real media preview/export at different resolutions, gesture cancel/history and regression suite; record results.

### Task 4: Recovery, offline media and background export (rank 6)

**Files:** new `src/recovery_store.{h,cpp}`, controller/engine integration, QML status/export/relink UI, tests.

**Interfaces:** `saveProject/loadProject` remain the persistence entry points; recovery path is separate and captures committed state only. `Media::exportMp4` gains optional cancellation callback with default for legacy callers. Existing atomic temporary-output behavior is retained. Relink replaces every matching path in one undoable command after validating candidate duration/type.

- [x] Write failing recovery tests for untitled state, two-second debounce/30-second maximum, failed writes, recover/discard and saved-file preservation.
- [x] Implement snapshot status/offers and offline placeholders with locate-file/folder matching; ambiguous candidates need explicit selection.
- [x] Prove frozen export while editing continues, cancellation before/during rendering preserving existing destination and cleaning only job temp output; retry settings persist.
- [x] Implement nonblocking compact export panel with destination, dimensions, 60 fps, codec, originals, captured revision, progress, Cancel, Reveal/Open and retry.
- [x] Verify full suite and failure/resource cleanup.

### Task 5: Integration, review and delivery

- [x] Sequentially merge the independent branches, resolving project schema, equality/history, preview and QML resource conflicts without dropping any subsystem.
- [x] Run a clean Release build and all tests, fix reproduced integration regressions and perform native light/dark/narrow render checks.
- [x] Fresh independent review against the full approved specs; reproduce and correct important findings.
- [x] Update feature/verification documentation accurately, compare delivery hashes to preserve concurrent changes, deliver and rebuild the real project.
