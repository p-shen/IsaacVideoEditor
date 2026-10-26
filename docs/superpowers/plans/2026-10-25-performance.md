# Preview and Export Performance Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the three approved preview/export speedups without changing the edit.
**Architecture:** Range-aware FFmpeg proxy cache, preview-only source offsets, and an MLT hardware export attempt with software fallback. Pause/requeue disposable proxy work around export.
**Tech Stack:** Existing C++17, Qt 6, MLT 7, FFmpeg.
**Spec:** docs/superpowers/specs/2026-10-25-performance-design.md

## Global Constraints

Preserve original media, native export size, 60 fps, source timing, rotation, fades, audio and atomic destination replacement. No additional dependencies or UI options. This folder is not a Git repository; work in place and retain a temporary baseline for review.

## Review Focus

Fractional-rate and variable-rate source timing; multiple trimmed pieces from one source; edits while old preparation finishes; failed/busy hardware that writes an audio-only file; no running preparation during failed or successful export.

### Task 1: Range cache and scheduling

**Files:** src/preview_cache.h, src/preview_cache.cpp, tests/preview_cache_test.cpp
**Interfaces:** Request{source,sourceFrames,inFrame,outFrame}; Copy{path,startFrame}; setRequests(ordered requests), readyCopy(source,in,out), prioritize(source,in,out), setPaused(bool). Retain existing full-source request/readyPath methods.

- [x] Write failing tests for range shortening/rebasing and covering-cache reuse, native cadence, priority, obsolete job cancellation, pause/resume.
- [x] Run the tests and confirm failure before implementation.
- [x] Implement validated/aligned ranges, fingerprinted cadence probe, covering-copy reuse, queue replacement, priority and pause/requeue.
- [x] Run preview cache tests, including legacy timing/audio/orientation tests.

### Task 2: Hardware export with fallback

**Files:** src/mlt_engine.cpp, tests/media_info_test.cpp
**Interfaces:** Existing exportMp4(Project,path,progress,error) remains unchanged.

- [x] Add a failing regression for unsuccessful rendering preserving the destination and for an actual exported video stream after a hardware failure.
- [x] Try macOS VideoToolbox at resolution-dependent bitrate, validate video and dimensions, rebuild/retry with libx264 on failure; retain temporary publication and progress.
- [x] Run media_info_test and actual hardware output verification.

### Task 3: Editor integration and verification

**Files:** src/main_window.h, src/main_window.cpp, tests/transport_test.cpp, README.md
**Interfaces:** Consume range-cache API; subtract source offset only in the temporary preview project; source project stays unchanged.

- [x] Add failing UI integration tests for partial proxy substitution and export preparation suspension.
- [x] Build ordered used-range requests and prioritize on playhead movement; track used paths per clip; preserve corrupt-cache fallback.
- [x] Pause preparation before starting the export worker; resume on either result after pending refresh.
- [x] Update usage notes, build Release, run all CTest targets, benchmark representative trimmed work and hardware export, obtain independent final review and address important findings.
