# Local Subtitle Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Generate SRT from the edited timeline, with local large-v3/Turbo switching and optional undoable import.

**Architecture:** A model downloader verifies pinned weights. A background subtitle job renders the captured timeline to PCM WAV, invokes an isolated bundled Whisper CLI, validates its result and publishes SRT atomically. The editing controller and existing Captions panel expose the workflow without changing the project format.

**Tech Stack:** C++17, Qt 6.6+, MLT 7, whisper.cpp v1.9.5/b5454 native worker, existing platform packaging.

**Spec:** `docs/superpowers/specs/2026-11-20-local-subtitle-generation-design.md` (approved by the user's instruction to implement).

## Global Constraints

- English/Mandarin transcription, Simplified Chinese, no translation or audio upload.
- Original media and captured timeline mix; 60 fps caption timebase.
- Model weights are downloaded separately, verified and stored outside projects.
- Cancel/failure preserves existing destinations and existing captions.
- Main acceptance target: Windows with NVIDIA RTX 4070 Ti; Mac verification cannot establish CUDA acceptance.
- Preserve all unrelated, already modified files in the shared checkout.

## Review Focus

- Missing/corrupt model or interrupted download must not become ready or replace a valid model.
- Model output and diagnostics are untrusted; invalid output must not publish.
- Timeline/source changes and repeated imports must not silently create current-looking duplicates.
- Non-ASCII paths, literal angle brackets, Chinese and emoji must survive the worker boundary.
- Windows worker runtime must be isolated from MinGW Qt libraries and not confuse NVENC with Whisper CUDA.

### Task 1: Verified model storage and selection

**Files:** create `src/subtitle_models.h`, `src/subtitle_models.cpp`, `tests/subtitle_models_test.cpp`; modify `CMakeLists.txt`.

**Interfaces:** `SubtitleModels(QObject*, QString directory, QNetworkAccessManager*)` exposes choices, selectedModel, busy, progress, ready, status and modelPath; slots `setSelectedModel`, `download`, `cancel`, `refresh`. A fixed `SubtitleModelSpec` catalog carries id/label/bytes/SHA256/URL; a C++ constructor test seam accepts fixture specs.

- [x] Write tests for selection/persistence, corrupt-cache validation, checksum rejection, verified success and cancellation with a small injected catalog/network reply.
- [x] Run the target first and observe failure before implementation.
- [x] Implement streaming download with QSaveFile, fixed HTTPS URLs/redirect policy, bounded reads, timeouts and background cache verification.
- [x] Build and run `subtitle_models_test`; all cases pass.

### Task 2: Isolated generation and safe publication

**Files:** create `src/subtitle_job.h`, `src/subtitle_job.cpp`, `tests/subtitle_job_test.cpp`; modify `src/gpu_pipeline.cpp`, `src/main.cpp`, `CMakeLists.txt`.

**Interfaces:** `SubtitleJob(QString appWorker, QString whisper, QObject*)`; `start(Project, destination, modelPath, cpuOnly, error)`, `cancel`, `stopAndWait`; signals stage/progress/device changes and `finished(bool success, bool canceled, QString error)`. Accessors return warnings, logPath and captured source identity validity. `runSubtitleAudioWorker(QStringList)` handles `--subtitle-audio-worker`. `subtitleEngineExecutable()` resolves only bundled engine or explicit developer override.

- [x] Write failing fixture-worker tests for success, malformed output, missing executable, no text, source collision, canceled job and destination preservation.
- [x] Implement asynchronous audio→Whisper lifecycle, bounded diagnostics, source identity checks and validated QSaveFile publication. Normalize Chinese with native platform mapping.
- [x] Extend the existing audio renderer for `.wav` mono/16 kHz/PCM while keeping `.m4a` unchanged.
- [x] Check actual WAV duration/sample content with synthetic cuts/mute/mix fixtures, including input longer than five minutes where practical without inference.
- [x] Run `subtitle_job_test` and affected GPU/audio export tests; all pass.

### Task 3: Controller and Captions interface

**Files:** create `src/subtitle_controller.cpp`, `qml/SubtitleGenerator.qml`, `tests/subtitle_controller_test.cpp`; modify `src/editor_controller.h`, `src/editor_controller.cpp`, `src/editor_jobs.cpp`, `src/qml_registration.cpp`, `qml/CaptionInspector.qml`, `qml/qml.qrc`, `CMakeLists.txt`.

**Interfaces:** controller properties `subtitleModels`, `generatingSubtitles`, `subtitleState`, `subtitleProgress`, `subtitleDevice`, `subtitleDestination`, `subtitleWarnings`, `canImportGeneratedSubtitles`, `subtitleEngineAvailable`, `subtitleCpuOnly`; invokables `generateSrtDialog`, `generateSrtTo`, `cancelSubtitleGeneration`, `importGeneratedSubtitles(bool confirmedAppend)`, `revealGeneratedSubtitles`.

- [x] Write tests for generation preserving dirty state/history, captured revision becoming stale, export exclusion, and one-step import/Undo with duplicate confirmation.
- [x] Add controller lifecycle/resource restoration, revision guard, QSettings model/device choice and close handling using existing patterns.
- [x] Add accessible model/download/generation/progress/cancel/result controls inside Captions. Label local device accurately and present explicit CPU mode.
- [x] Run controller/QML checks and inspect keyboard operation, cancellation and completed import in the native app.

### Task 4: Reproducible native engine packaging

**Files:** create `packaging/prepare-whisper.py`, `packaging/test_prepare_whisper.py`; modify platform packaging scripts, `.github/workflows/release.yml`, `packaging/README.md`, `CMakeLists.txt`.

**Interfaces:** prepare script writes a runtime directory with `whisper-cli[.exe]`, native dependencies, version manifest and license notices. CMake `ISAAC_WHISPER_RUNTIME_DIR` copies this directory into the app's `subtitles` folder. Windows uses pinned official b5454 CUDA 12.4 archive (SHA256 `afef0b881c500958921c3f5523b50e59ee2ec9b6f5cbd25b324c51ed308a957a`), isolated from application DLL search paths. Mac builds the pinned native source with embedded Metal resources; Linux uses pinned source/build or upstream matching runtime.

- [x] Write failing archive-integrity/path-validation/runtime-layout tests.
- [x] Implement safe preparation, include worker notices/runtime in platform packages and release builds, and reject incomplete runtime directories.
- [x] Run packaging helper suites and a Mac packaged-runtime smoke check; record Windows CUDA as pending without hardware.

### Task 5: End-to-end verification and documentation

**Files:** modify `docs/captions.md`, `docs/building.md`, the spec status and this plan's checkboxes; preserve ignored media/model artifacts.

- [x] Download verified full large-v3 and Turbo weights to the application model store and exercise generation on a private project using `test.mp4`; retain prior Qwen/Whisper files unchanged.
- [x] Inspect/import generated SRT through existing parser/controller, saving separate model results for listening comparison.
- [x] Run full CTest and applicable packaging tests, inspect the app, and request one fresh review of the feature diff relative to its captured dirty baseline.
- [x] Fix material findings with regression checks, document limits and deliver the app/output paths.

Implementation stays uncommitted while unrelated user work shares this checkout; review uses a captured file baseline so earlier changes are not attributed to this module.

Completed November 20, 2026. Final macOS run: 39/39 CTest targets passed (211.67 seconds). Both verified full models completed real generation and import/Undo; signed Mac package passed clean-runtime smoke checks. Windows CUDA/Unicode execution and Linux execution remain pending on those platforms. Listening comparison is left for user review.
