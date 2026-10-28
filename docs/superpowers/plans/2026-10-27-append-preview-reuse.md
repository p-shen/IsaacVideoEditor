# Append Preview Reuse Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task. Work in the current checkout to retain its existing uncommitted UI changes.

**Goal:** Append clips without reopening unchanged video decoders or replacing all filmstrip thumbnails.

**Architecture:** Carry prepared video producers from the stopped previous timeline into the replacement when their source and decoding settings match. Select cached filmstrip frames within each tile's source interval. Rebuild timeline structure and non-video tracks normally.

**Tech Stack:** C++17, Qt 6, MLT 7, existing QtTest targets.

**Spec:** docs/superpowers/specs/2026-10-27-append-preview-reuse-design.md

## Global Constraints

- Use existing Qt/MLT dependencies; keep project serialization and export unchanged.
- Preserve undo/redo, output sizing, exact trim comparison, and automatic fitting.
- Preserve unrelated local edits; no automatic commit or branch changes.

## Review Focus

- Changed or missing media must not retain a stale decoder.
- Profile changes must not retain a producer from another raster.
- Reused clip transforms must remain editable after the prior timeline is destroyed.
- Filmstrip frames must respect trim intervals and rotation; exact requests stay exact.
- Proxy replacement, transitions, music, captions, and undo must still render correctly.

## Task 1: Reuse prepared playback sources

Files: src/mlt_engine.{h,cpp}, src/visual_renderer.cpp, src/editor_controller.cpp, tests/media_info_test.cpp, tests/editor_controller_test.cpp.

- [x] Add regression tests comparing actual MLT source handles across append/replacement builds and checking changed source/profile/audio invalidation and live transform edits.
- [x] Run the tests against the existing behavior and confirm the missing reuse fails.
- [x] Add an optional stopped previous timeline to Media::buildTimeline; retain prepared clip producers on the timeline, validate source identity/settings, and register reused transforms.
- [x] Retain the previous timeline through controller rebuild and supply it to the replacement builder.
- [x] Verify new tests plus preview, composition, and source-browser tests.

## Task 2: Reuse filmstrip frames within visible intervals

Files: src/media_cache.{h,cpp}, src/quick_timeline.cpp, tests/media_cache_test.cpp, tests/quick_timeline_test.cpp.

- [x] Add tests for reuse of completed and pending frames, trim/rotation boundaries, and append with automatic fitting.
- [x] Run tests and confirm the existing midpoint-only requests fail.
- [x] Add a bounded filmstrip request that uses a nearby completed or pending frame within the tile interval, falling back to its midpoint; use it only in QuickTimeline.
- [x] Verify new tests and existing exact-frame/orientation tests.

## Completion

- [x] Build the application and run the full CTest suite.
- [x] Request a read-only review and address findings.

## Execution notes

The user authorized fixing the diagnosed performance issue. Implementation proceeds inline with these focused choices; no additional product decisions are required.

Verified: application built in build-append; all 26 CTest targets passed with normal preview-cache access. Regression tests were observed failing before decoder/thumbnail reuse was implemented. Review's tile-boundary issue was reproduced and corrected. Timeline structure and non-video tracks are still rebuilt; unchanged main-video decoders and eligible filmstrip frames are reused. Large user projects have not been timed.
