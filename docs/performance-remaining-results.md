# Remaining performance implementation results

November 1, 2026 · Based on `d1149b8` · Branch `codex/remaining-performance`

The remaining audit implementation adds background UI media/graph preparation, cached timeline data, bounded persistent proxy storage, thumbnail batching, reusable source timing checks, directional frame preloading, selectable preview quality through 4K, scene-graph video textures, optional NVIDIA decode/scale/composition, and diagnostics plus a reproducible benchmark runner. Native resolution remains the export default; iPhone optimization remains optional.

## Verification

A fresh Release build passed all **34 CTest targets in 212.31 seconds** on macOS 15.7.3 (arm64), Qt 6.12.0, MLT 7.42.0. The first integrated run identified asynchronous test assumptions and a cache-folder isolation issue; both were repaired without weakening the existing result/pixel assertions. Focused checks also caught and fixed a retained-proxy lifetime race, missing-file regeneration, speculative-preview error handling, and failed-metadata retry ordering. The media retry regression passed ten consecutive focused runs.

The benchmark playback report subsequently gained a deterministic silent audio driver and explicit startup/error/frame counters after the native default audio device failed to start. Both affected CLI/schema targets passed again (2/2 in 10.57 seconds), including a deliberately unavailable audio-driver regression.

The native Mac interface was checked for the File → Performance panel, all four quality choices, cache controls, and Escape dismissal. The normal `build/IsaacVideoEditor.app` was updated from the verified build; its executable SHA-256 matches the tested binary. The installed app also passed its QML/SVG/FFmpeg, media decode, playback, and MP4-export smoke check. The previous app is retained at `build/.IsaacVideoEditor-before-remaining-performance.app`.

The regressions cover stale/canceled imports and graph results, undo/recovery, source replacement, invalid copies, cross-process cache readers, active-file quota protection, directory/quality changes, VFR/nonzero-origin timing, exact trim versus batched filmstrips, 4K preview profile ownership, speculative errors, software export supervision, and all generated benchmark project formats.

The CUDA test covers the supported-plan selection, unavailable runtime, driver-loss fallback, cancellation, CPU analogue composition/color geometry, and unchanged audio. Actual NVIDIA native/iPhone output comparisons require `ISAAC_RUN_CUDA_ACCEPTANCE=1` on the workstation; the two local hardware cases skip on this Mac.

## Local measurements

[Machine-readable results](performance-remaining-results.json) contain binary hashes, all seek samples, budgets, stage/encoder identity, memory, and validation limits. Three paired serial runs used two-second synthetic 1080p60 projects, alternating revision order. Timings include process startup, hardware probing, internal validation, and publication. There were no concurrent builds or tests.

All **36 exports decoded to exactly 120 video frames**. One complete software-output pair for each workload matched pixel for pixel. Automatic mode used Apple VideoToolbox on this Mac; software mode used libx264. The processing pipeline was MLT.

| Workload | Mode | Baseline median | Current median |
| --- | --- | ---: | ---: |
| overlay | software | 2.45 s | 2.44 s |
| overlay | auto | 2.76 s | 2.80 s |
| plain | software | 1.00 s | 1.00 s |
| plain | auto | 1.38 s | 1.40 s |
| text | software | 3.14 s | 3.18 s |
| text | auto | 3.50 s | 3.34 s |

These short-run differences are not a dependable speedup or regression. The added GPU export path targets NVIDIA and was not active on this Mac. The local host has eight logical CPUs and 8 GiB total RAM; available memory selected conservative budgets.

| Workload | Load | First-sweep seek median | Repeated seek median |
| --- | ---: | ---: | ---: |
| plain | 120 ms | 53.1 ms | 2.5 ms |
| overlay | 118 ms | 68.9 ms | 2.6 ms |
| text | 248 ms | 71.5 ms | 2.6 ms |

Each sweep covers the same five positions. The first visible seek may include initial window/Metal setup; these are delivery timings, not a baseline-versus-current editing comparison. All three visible playback samples started, delivered frames, and reported no errors. Qt reported Metal (graphics API 6), with texture uploads recorded. The frame/skip counters in the JSON describe this host and do not claim 60 fps on the workstation.

## Workstation acceptance still required

Run the benchmark matrix and a long representative project on the Threadripper/4070 Ti. Record operating system, physical/logical cores, RAM, GPU driver, source/cache/output drive models, actual pipeline/encoder, independent GPU engines, preview latency during preparation/export, steady-state memory, frame completeness, audio synchronization, and cancellation. Test a new optimized NVIDIA output in iPhone Photos.

The CUDA subset is deliberately strict. Text, transitions, rotation, animated transforms, unsupported color/timing, and geometry without established-renderer parity use MLT. Source eligibility checks are bounded per source within each export; short-segment process startup may outweigh GPU savings. Worker RSS excludes separate FFmpeg processes. No NVIDIA speedup or long-run memory claim can be established by the Mac checks.
