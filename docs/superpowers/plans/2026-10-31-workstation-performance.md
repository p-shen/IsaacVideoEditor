# Workstation performance implementation plan

> **For agentic workers:** Use the parallel-agent workflow for the independent encoding, background-preparation, and compositor tasks. The coordinating agent owns the resource policy, preview caching, integration, and final review. Steps use checkbox syntax for tracking.

**Goal:** Implement the high-priority recommendations approved by the user's request to implement the performance audit.

**Architecture:** Keep Qt, MLT, FFmpeg, and isolated export. Add a small shared resource policy, hardware H.264 selection with runtime fallback, bounded background concurrency, memory-aware frame and media caches, and less serialized CPU compositing. Scale preview resolution to capable hardware within a coordinated 1080p limit.

**Tech stack:** C++17, Qt 6.6+, MLT 7, FFmpeg, CMake, QtTest.

**Spec:** [Editing and export performance audit](../../performance-audit.md), findings 1–6. GPU-resident decoding/compositing is a later profiling-dependent architecture option; implement the immediate CPU-path improvements identified in finding 5.

## Global constraints

- Preserve export isolation, frozen revisions, original-media rendering, complete frames, audio timing, safe publication, cancellation, and software fallback.
- Do not add dependencies or require NVIDIA drivers on computers without an NVIDIA GPU.
- Use actual runtime encode attempts to establish hardware support; listing an encoder is insufficient.
- Cache keys must include source identity, render dimensions, and edit/producer revision. Bound caches and concurrent jobs; reduce budgets when memory is constrained.
- Keep priority seeks responsive, discard stale jobs and frames, preserve cadence and proxy source offsets.
- Work in the existing `codex/export-reliability` checkout; preserve the audit files. Do not commit, push, publish, or create a PR as part of this request.
- Each owner writes a regression first and observes its failure before implementation, then runs focused checks. Coordinate CMake edits through the parent. Use separate build directories for simultaneous builds.

## Shared interfaces

`src/performance.h` (parent owns) defines namespace `Performance`:

```cpp
struct Resources { int logicalCpus=1; quint64 totalMemoryBytes=0, availableMemoryBytes=0; };
struct Budget {
    int renderThreads=1, proxyJobs=1, mediaJobs=1, backgroundThreads=1;
    int previewWidth=960, previewHeight=540;
    int thumbnailBytes=16*1024*1024, waveformBytes=8*1024*1024;
    qsizetype frameCacheBytes=32*1024*1024;
    bool prepareDuringExport=false;
};
Resources resources();
Budget budgetFor(const Resources &resources);
Budget budget();
```

Runtime policy uses affinity-aware logical CPU count and available/total RAM. Unknown hardware keeps conservative defaults. Each Preview owns a bounded frame cache; the two preview budgets together remain a fraction of available RAM. The policy caps preview at 1080p on capable hardware and background concurrency at four jobs total across media and proxies.

`src/encoding.h` (encoding owner) defines namespace `Media`:

```cpp
QString hardwareH264Encoder(); // h264_nvenc on Windows/Linux; h264_videotoolbox on macOS
bool isHardwareH264Encoder(const QString &encoder);
QString h264EncoderLabel(const QString &encoder);
```

The proxy owner can try the selected hardware codec through its asynchronous FFmpeg process and retry software once; disable failed hardware selection for subsequent jobs in that cache instance. Export probes through its actual MLT consumer in the isolated worker, selects workload-sensitive negative real_time and bounded buffers, and logs the selected budgets.

## Review focus

- Hardware codec exists but fails at initialization, midway through encoding, or crashes the worker: software retry remains bounded and preserves output.
- Source is replaced, removed, or trimmed while multiple jobs finish: stale outputs cannot become ready or overwrite a new generation.
- Cached still is requested after an edit, producer replacement, profile change, or pending visual edit: no stale frame appears.
- Low/unknown RAM and many logical CPUs: no zero-size buffers, unbounded allocations, or excessive worker multiplication.
- Hidden or inactive overlays, Unicode text, fades, and concurrent frame rendering: preserve timeline and color behavior, with comparison against the correct no-overlay frame path.

## Tasks

### 1 Resource policy and playback caches

**Owner:** parent. **Files:** `src/performance.h/.cpp`, `src/preview.h/.cpp`, `src/editor_controller.cpp`, new `tests/performance_test.cpp`, existing preview tests, CMake.

- [ ] Write policy tests for conservative, low-memory, 8-core, and 32/64-core 168 GiB hardware; observe failure.
- [ ] Implement platform resource discovery and bounded policy; make tests pass.
- [ ] Add tests proving repeated seeks reuse decoded frames, pending edits invalidate cached pixels, replacement/profile changes retire the cache, and eviction respects byte budgets; observe failures.
- [ ] Add a thread-safe byte-bounded QImage LRU to Preview, with revision invalidation and playback-frame reuse. Coordinate controller/source/renderer preview dimensions and ready proxy use.
- [ ] Verify focused policy and preview suites, then record results in the progress ledger.

### 2 Hardware encoding and export scaling

**Owner:** encoding agent. **Files:** new `src/encoding.h/.cpp`, `src/mlt_engine.cpp`, `src/export_job.cpp`, `src/editor_jobs.cpp`, export/media-info tests, new encoding tests if needed. Parent owns CMake integration.

- [ ] Add failing tests for NVIDIA recognition/labeling and hardware-crash retry, runtime probe failure, negative render-worker policy, complete output, and source protection.
- [ ] Implement actual MLT hardware probe, NVENC/VideoToolbox configuration, bounded software fallback, workload-aware render threads and buffers, and truthful status/logging.
- [ ] Verify focused export tests and record failure/pass evidence. Return changed files and any platform limitations.

### 3 Background preparation and media cache budgets

**Owner:** preparation agent. **Files:** `src/media_cache.h/.cpp`, `src/preview_cache.h/.cpp`, their tests. Parent integrates export pause policy.

- [ ] Add failing tests for simultaneous jobs within the policy limits, priority thumbnails completing despite a blocked waveform, source replacement/cancellation with multiple jobs, and hardware proxy fallback.
- [ ] Implement per-job process/output ownership, bounded queues and concurrency, explicit FFmpeg thread limits, media cache budgets, dimension-aware proxy fingerprints, and asynchronous hardware proxy fallback.
- [ ] Preserve source freshness, timing phase, range offsets, temporary file ownership, progress and pause behavior. Verify focused suites.

### 4 Compositor work and safe concurrency

**Owner:** compositor agent. **Files:** `src/visual_renderer.cpp`, `src/text_renderer.cpp`, composition/text tests.

- [ ] Add failing tests for inactive/hidden overlay bypass against the no-overlay color path, repeated still-source reuse, concurrent text rendering equivalence, fades and boundaries.
- [ ] Avoid inactive image callbacks, share bounded immutable still images per source identity, load stills lazily, narrow overlay state locking where safe, and use thread-local bounded text layouts to avoid the global drawing lock.
- [ ] Preserve active overlay output and text geometry/fades. Do not claim exact equivalence to the old inactive RGBA round trip: validate the corrected path against plain video. Verify focused suites.

### 5 Integration and verification

**Owner:** parent. **Files:** controller/jobs, CMake, documentation, integration tests.

- [ ] Integrate new sources/tests, preserve preparation alongside export only when policy permits, and ensure both previewers share coherent dimensions and independent budgets.
- [ ] Build all targets; run the complete CTest suite and packaging script tests appropriate to runtime changes.
- [ ] Compare real software exports, complete decoded-frame counts and active composition output. Measure baseline/current representative workloads serially after all parallel builds finish.
- [ ] Obtain a fresh independent review; address material findings with regressions and rerun affected/full checks as needed.
- [ ] Update performance documentation with implemented behavior, actual measurements, and NVIDIA/Threadripper validation limits. Leave a reviewable working-tree diff.
