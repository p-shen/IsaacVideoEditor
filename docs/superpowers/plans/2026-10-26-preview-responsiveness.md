# Preview Responsiveness Implementation Plan

> Implement inline with superpowers:executing-plans and test-driven development.

**Goal:** Lower playback work and keep visual drags responsive.
**Architecture:** Reuse PreviewCache, the Preview seek worker and existing MLT visual filters; cache timeline background artwork.
**Tech Stack:** C++17, Qt Quick, MLT, FFmpeg.
**Spec:** docs/superpowers/specs/2026-10-26-preview-responsiveness-design.md

## Global Constraints
Preserve original media, saved source timestamps, attachment and animation timing, orientation, undo/redo, gesture cancellation and native export. No dependencies. No Git repository; source baseline: /private/tmp/isaac-performance-baseline. Do not restart the open editor with unsaved work.

## Review Focus
- Partially cached overlays must use original source-frame animation time.
- Hidden, offline, or failed overlay proxies must retain fallback behavior.
- Seek replacement and producer destruction must not lose edits or access freed filters.
- Crossfaded clips need independently updated transform states.
- Selection, zoom, palette, media completion, trim and keyboard focus must invalidate or redraw the correct timeline layers.

### Task 1: Overlay preview copies
Files: src/project.h, src/editor_controller.{h,cpp}, src/visual_renderer.cpp; tests/editor_controller_test.cpp, tests/composition_test.cpp.
Interfaces: reuse PreviewCache::Request/Copy/readyCopy; add runtime-only VisualOverlay::sourceFrameOffset analogous to Clip.
- [x] Add a real high-resolution overlay preparation test and a partial-copy animation-origin regression; run and observe failures.
- [x] Include visible overlay ranges in preparation, readiness, fallback and playhead priority; substitute only the temporary preview project.
- [x] Verify frame offsets and unchanged persisted/export source paths with focused tests.

### Task 2: In-place asynchronous visual edits
Files: src/preview.{h,cpp}, src/visual_renderer.{h,cpp}, src/mlt_engine.cpp, src/composition_controller.cpp; tests/composition_controller_test.cpp, tests/composition_test.cpp, tests/preview_async_test.cpp.
Interfaces: Preview::requestSeek accepts an optional worker edit; VisualRendering::update changes registered transform and overlay filter states without opening media.
- [x] Add a regression requiring drag updates to return before frame delivery, and worker coalescing/lifetime tests; observe failures.
- [x] Retain transform filter states by clip identity, including identity transforms. Apply edits on the seek worker; retain pending edits when ordinary seeks replace a request.
- [x] Keep one rebuild on gesture commit/cancel and structural changes; verify pixels, crossfades, cancel and history.

### Task 3: Timeline artwork cache and delivery
Files: src/quick_timeline.{h,cpp}, tests/quick_timeline_test.cpp, README.md.
Interfaces: paintContents handles cached artwork; paint draws dynamic indicators; invalidateContents marks static artwork dirty.
- [x] Add a paint-operation regression for playhead-only movement and pixel/invalidation checks; run baseline benchmark and observe failure.
- [x] Cache the static artwork at display density; invalidate all content mutations and retain dynamic playhead/skim/focus rendering.
- [x] Run focused tests and comparable paint benchmark, then Release build and complete CTest suite.
- [x] Obtain one independent final code review and address material findings with regressions.
