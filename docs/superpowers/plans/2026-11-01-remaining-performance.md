# Remaining performance work

**Goal:** Implement the remaining editing/export performance audit recommendations while retaining native-resolution exports, source timing, undo/recovery, and safe software fallback.

**Architecture:** Keep Qt Quick, MLT, and FFmpeg. Give expensive preparation independent ownership and revision checks; cache derived timeline data, bound persistent media storage, prepare likely preview frames, and choose GPU processing only for supported runtimes and project features. Keep GPU capability and actual workstation measurements distinct from simulations.

**Scope:** Audit findings 3–5 and 7–8, and the workstation acceptance measurements described in `docs/performance-audit.md`. Native resolution stays the export default; iPhone optimization remains optional.

## Work packages

- [x] Editing responsiveness: asynchronous UI import/probing and timeline preparation, one settled still, stale-job cancellation, cached clip boundaries/source usage, narrower playback notifications.
- [x] Media preparation/storage: bounded proxy disk cache, safe LRU eviction and active-file protection, selectable local directory/quota, reusable timing checks and batched nearby thumbnails.
- [x] GPU processing: runtime-tested hardware decoding/scaling/composition for supported projects, truthful fallback for unavailable capabilities or unsupported effects, preserved audio/timing and safe publication.
- [x] Preview: bounded directional frame preloading with priority seeks, revision/source invalidation, coordinated higher-resolution preview choice and GPU texture presentation.
- [x] Diagnostics/acceptance: cache hit/miss/preload counts, delivered/skipped preview frames, latency and stage timings, process memory, repeatable hardware benchmark command and report.
- [x] Integration: settings UI, focused regressions and full suite, synthetic decode/export/color checks, independent review, updated audit status, and update the launchable Mac development app.

## Verification and constraints

Test stale completion after edits/relink/close, imports canceled or superseded, copied cache folders with unrelated files, cross-process cache ownership, disk quota smaller than active working files, variable frame rates and rotations, rapid seeks while preloading, GPU initialization and mid-export failure, and native versus iPhone mode. Match compositor geometry, color, fades, text, frame count, and audio timing; unsupported GPU combinations retain the established CPU renderer. Keep new dependencies optional and preserve machines without NVIDIA. Never delete original media or arbitrary cache-directory contents.

Record actual local build/test/benchmark evidence and leave actual NVIDIA/Threadripper acceptance explicitly pending until it runs on that hardware. Do not treat a skipped hardware test as a hardware success.
