# Export reliability and text performance implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Follow test-driven development. The independent text cache task may run in parallel under superpowers:dispatching-parallel-agents.

**Goal:** Keep a native export crash from closing the editor, capture useful diagnostics, and reduce repeated text rendering work for cuts, audio, and text projects.

**Architecture:** The editor launches the same application in a minimal export-worker mode with a frozen project snapshot. The parent owns a unique staging directory beside the destination and publishes only validated worker output. Structured progress, encoder diagnostics, bounded log output, cancellation, and process-exit handling preserve the current nonmodal workflow. Text rendering reuses a bounded cache of active text data and preserves appearance, fades, and live edits.

**Tech Stack:** Existing C++17, Qt 6.6+, MLT 7, FFmpeg. No additional dependencies.

**Spec:** docs/export-investigation.md and the user's approval after narrowing the project to cuts, audio, and text overlays.

## Global Constraints

- Preserve 60 fps, original media, H.264/AAC, canvas, audio, transitions, fades, text positioning, and frozen-revision retry.
- Preserve source-file collision protection and an existing destination on failure/cancel/crash; publish atomically only on success.
- Keep the current working checkout's unrelated edits. No automatic commits of shared files.
- Reuse the installed application as the worker so release packages contain the required executable on all platforms.
- Avoid changing queue defaults without reliable workload evidence; bound text-cache memory instead.

## Review Focus

- Crash and malformed worker output must not be accepted as success, including when an old destination already exists.
- Cancellation during startup/render/finalization must preserve the destination and remove only this job's staging files.
- A late cancellation after successful publication must not change a completed export to canceled.
- Launch from tests and packaged installations must find the actual application and initialize fonts/MLT without an editor window.
- Long captions, overlapping titles, noninteger canvas scales, text edits, fades, and many cues must retain their visual behavior without unbounded caching.

## Task 1: Isolate export and capture diagnostics

**Files:** Create src/export_job.h, src/export_job.cpp, tests/export_job_test.cpp. Modify src/main.cpp, src/editor_jobs.cpp, src/editor_controller.h, src/editor_controller.cpp, src/mlt_engine.h, src/mlt_engine.cpp, CMakeLists.txt, docs/advanced-editing.md.

**Interfaces:** ExportJob QObject owns QProcess and staging, emits progressChanged(int), diagnostic(QString), finished(bool, bool, QString). start(Project, destination, QString *error) freezes input; cancel() stops safely; stopAndWait() provides bounded shutdown. workerMain(snapshotPath, stagedOutput, softwareOnly) calls existing Media::exportMp4; child-mode arguments are handled before constructing the editor. Optional export diagnostics callback and software-only selection are appended to the existing engine function with defaults, preserving callers.

- [x] Add failing behavior tests for actual worker export, killed worker preserving destination/editor state, failed launch, cancellation and cleanup, retrying the frozen revision, and readable bounded diagnostics.
- [x] Run the tests before implementation and confirm missing worker behavior.
- [x] Implement parent-owned staging and atomic publication, minimal worker entry, structured protocol, persistent bounded per-job diagnostics, and one software retry after a worker crash during hardware encoding.
- [x] Integrate the existing controller lifecycle and panel without changing unrelated editing behavior.
- [x] Run export-job and recovery/export tests, actual hardware selection/fallback tests, and package smoke test.

## Task 2: Cache text with bounded memory

**Files:** Modify src/text_renderer.cpp, src/text_renderer.h if needed, tests/text_overlay_test.cpp. Do not edit shared CMake or controller files.

**Interfaces:** Preserve TextRendering::bounds/paint/attach/update and the MLT filter contract. Reuse active text layouts or raster data with a bounded cache; invalidation follows text/style/position/canvas changes. Preserve fade rendering and layering.

- [x] Add a meaningful regression proving repeated frames reuse expensive text work, plus visual regressions for fades, overlapping text, edits, and scaled canvases.
- [x] Observe the regression fail before implementation.
- [x] Implement the smallest bounded cache that preserves rendering behavior. Keep test-only hooks out of production classes.
- [x] Run text-overlay/caption/composition tests and compare actual text-heavy export performance and pixels.

## Task 3: Verify integration and document

- [x] Build all targets and run the full CTest suite, identifying any unrelated pre-existing failures explicitly.
- [x] Verify a real text/audio export and hardware behavior with normal encoder access; inspect dimensions, frame count, text pixels, and audio.
- [x] Verify a deliberately killed worker leaves the editor editable and the destination intact; verify retry/cancel cleanup.
- [x] Request a fresh code review focused on process lifecycle, source/output safety, cache correctness, and portability; address important findings.
- [x] Update export usage and investigation status with measured results and remaining full-project reproduction limits.
