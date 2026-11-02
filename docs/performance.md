# Workstation performance

The editor automatically selects bounded CPU and memory budgets. Windows and Linux exports and preview preparation try NVIDIA H.264 encoding when the installed runtime and GPU support it. Macs try VideoToolbox. Failed hardware initialization falls back to software H.264.

## Editing

Recently displayed frames are retained for faster repeated scrubbing. Cache validity includes the producer revision and source path, size, and modification time. Text and visual edits invalidate old frames; replacing a source also retires cached pixels. Memory limits shrink when available RAM decreases.

Automatic quality selects up to 1080p on capable machines and 540p on smaller machines. **File → Performance…** offers explicit 540p, 1080p, and 4K limits, applied together to timeline preview, source preview, and generated copies. The original canvas and native export resolution are unchanged. Increasing quality can require new copies; it does not upscale the original media. The source viewer can use a full-source preview copy and retries the original if that copy is damaged. Partial copies remain available to the timeline without changing source-viewer positions or playback length.

On machines with at least twelve logical CPUs and 8 GiB available RAM, a bounded directional queue also prepares up to sixteen likely next frames. Foreground seeks take priority; speculative frames enter the cache without changing the displayed frame or hiding requested-frame errors. The budget reserves room for working frames and shrinks under memory pressure. Video presentation uses a reusable Qt Quick scene-graph texture, with a software backend available. Only editing decorations use a separate painter.

Background preparation has independent process and output state for every job. On a workstation it permits two proxy jobs and two thumbnail/waveform jobs, with explicit decoder, encoder, and filter thread limits. Urgent thumbnails can preempt a long waveform. Machines with spare capacity continue preparation during playback and export; conservative budgets defer ordinary interactive work and pause proxy preparation during export.

Nearby nonpriority filmstrip requests share one FFmpeg process, with at most eight frames across two seconds. Exact trim requests keep their existing frame accuracy and can preempt batching. Successful constant-cadence source checks are persisted by source identity and reused across preview ranges and quality changes. Variable cadence retains the timing-correct preparation fallback.

UI imports, project loading, relinking, audio inspection, recovery, source-preview preparation, and timeline graph construction use bounded workers. Prepared results carry the captured revision/source identity; stale results cannot replace newer edits. A prepared graph requests one asynchronous still at the intended position. Cached clip boundaries and asset usage avoid repeated project scans during playback and viewport refresh. Synchronous programmatic import/load helpers remain available for existing callers. Native decoder work already running may finish before its stale result is discarded.

**File → Performance…** also selects the preview storage folder and quota (10 GiB by default). Eviction removes only application-owned, unused artifacts in least-recently-used order. Active producer leases, cross-process readers, and in-progress writes protect their files, so a working set may temporarily exceed the quota. A zero quota retains active files and prunes inactive owned artifacts. Unmarked older files and unrelated directory contents are preserved and excluded from accounting. Changing folders leaves the old folder intact.

Photos load when their overlay becomes active. References to the same canonical source identity share decoded pixels across timelines within the process. Active references and in-flight frames retain their pixels, avoiding repeated decoding when the inactive-image cache fills. The inactive-image LRU is bounded at 32 MiB. Distinct simultaneously active images still require their decoded working memory. Text layout caches are bounded per rendering thread, allowing independent frame rendering without a global text-drawing lock.

## Export

Exports retain native canvas resolution by default. The optional **Optimize for iPhone** setting fits the output within a landscape 3840 × 2160 or portrait 2160 × 3840 envelope without upscaling or changing the project. It uses H.264 High at Level 4.2 for HD-sized rasters and Level 5.2 for larger rasters, with peak bitrate limits of 16 or 40 Mbps and a two-second rate-control buffer. NVIDIA, Apple, and software encoders receive the same compatibility policy. Hardware output validation checks the resulting level and pixel format, and retries preserve the selected mode.

This addresses an iPhone 15 Plus Photos import failure with a 2880 × 3840, 60 fps, H.264 High Level 6.0 export at approximately 112 Mbps. The original-format short sample could not be imported; the user confirmed successful imports of 2160 × 2880 Level 5.2 and 1080 × 1440 Level 4.2 samples. These samples used software encoding, so a new optimized NVIDIA export still needs a device check.

Plain edits use one MLT frame-processing worker while FFmpeg retains its automatic codec threading. Compositions can use up to eight frame-processing workers, limited by CPU count and the memory required for the output dimensions. Export always disables frame dropping.

Queues reserve space for working frame copies as well as buffered frames. Their memory allowance is at most one eighth of available RAM, capped at 2 GiB. This permits parallel 4K rendering on a large workstation without forcing large queues onto a smaller computer. Queue estimates do not include every allocation made by codecs or the operating system.

Hardware capability is checked with a short encode through the actual MLT consumer in the isolated worker. A parent-side ten-second deadline handles a driver wedged during probing. A hardware crash or stalled probe gets one software retry. Cancellation suppresses retry. Missing or corrupt overlay images produce an explicit render error so an incomplete composition cannot replace an existing output.

For a supported NVIDIA setup, the isolated export worker can use an optional external FFmpeg pipeline: NVDEC decode → CUDA scale/composition → NVENC encode. It runs an actual source-frame probe before committing to this path. The initial supported subset is progressive, square-pixel, tagged BT.709 limited-range 8-bit H.264/HEVC with verified zero-origin constant 60 fps timing; hard cuts, static fit/fill/position/scale, video overlays, and still-image overlays are supported when their geometry matches the established renderer. Still alpha is prepared once per segment/layer and uploaded for composition. Audio uses the existing MLT mixer and is muxed with the GPU video.

Transitions, text, animated transforms, opacity changes, rotation, unsupported color/timing/geometry, unavailable CUDA filters, and runtime GPU failure select the established MLT path with an explicit reason. GPU rendering uses original media and preserves native/iPhone selection, frozen revisions, cancellation, validation, and safe publication. Source eligibility uses a bounded packet scan per source within each export (32 MiB/15 seconds), separate from the persistent preview timing cache. Segment subprocess startup can limit gains on short clips.

The export log identifies the actual pipeline/encoder, worker/queue settings, available memory, fallback reasons, frame throughput, stage durations, and worker resident/peak memory. The MLT video stage includes audio work; its diagnostics say so. Worker memory excludes separate FFmpeg children and other processes. Keep this log when comparing machines or reporting an export problem.

## Example resource budgets

These examples describe policy inputs, rather than measured performance on the named hardware. Budgets allocate space only as content needs it.

| Resource | 8 CPUs with 4 GiB available | 32 CPUs with 168 GiB total and 128 GiB available |
| --- | ---: | ---: |
| Maximum composition workers before raster limits | 4 | 8 |
| Proxy jobs plus media jobs | 1 plus 1 | 2 plus 2 |
| Threads per background stage | 2 | 4 |
| Preview limit | 960 by 540 | 1920 by 1080 |
| Recent frames per viewer | 256 MiB | 8 GiB |
| Thumbnail cache | 32 MiB with 16 GiB total RAM | 256 MiB |
| Waveform cache | 8 MiB with 16 GiB total RAM | 64 MiB |
| Export working and queue allowance | 512 MiB | 2 GiB |

Available memory below 2 GiB reduces frame processing and background thread counts. Unknown hardware information uses conservative budgets. The two viewers have independent frame caches; their combined allowance remains a fraction of available memory.

## Diagnostics and workstation benchmark

The Performance dialog exposes cache hits/misses, prepared frames, delivered/skipped playback frames, seek/decode time, preparation work, storage eviction, resource budgets, and process memory. These are counters for the running session, not a workstation speed claim.

Generate synthetic source footage and exercise the actual application with Python 3 and FFmpeg on PATH:

```sh
python3 tools/benchmark_performance.py --editor /path/to/IsaacVideoEditor --output /path/to/new-results --matrix --repeat 3 --seconds 5 --visible
```

On macOS, the executable is inside `IsaacVideoEditor.app/Contents/MacOS/IsaacVideoEditor`; on Windows use `IsaacVideoEditor.exe`. The folder must be new. `--matrix` covers 1080p/4K H.264 and HEVC; the plain, overlay, and text cases exercise accelerated and fallback workloads. Both software and automatic exports are independently decoded for frame completeness. `--iphone` repeats the selected cases with the compatibility mode. `--baseline-editor /path/to/older/executable` compares both revisions through the same worker protocol; older builds without metrics are labeled unavailable. Keep media/cache/output on the drives being evaluated, and record those drive models separately.

The results include binary hashes, platform/CPU information, output free space, FFmpeg version, actual export stages, and cold/warm seek reports. `--visible` includes a real preview window and texture-upload counts; headless reports explicitly exclude display presentation. The playback sample uses SDL’s silent audio driver by default, measuring decoding/presentation rather than an output device. An explicitly supplied audio driver is respected. Playback startup, elapsed time, frame counts, and errors are reported separately; zero delivered frames cannot masquerade as a successful playback measurement. If `nvidia-smi` is available, whole-GPU encode/decode/graphics utilization and memory samples are recorded separately at roughly one-second intervals. Unavailable readings are absent. Other GPU applications affect those samples, and short runs can miss peaks. Sampler shutdown is excluded from export elapsed time.

Individual commands are also available:

```sh
IsaacVideoEditor --performance-report project.isaac.json new-report.json --visible
IsaacVideoEditor --benchmark-export project.isaac.json new-video.mp4 --software
```

`--benchmark-export` uses the same supervised worker/fallback/publication path as the UI. Remove `--software` for automatic selection. Its generous deadline cancels stalled benchmark exports; it is not a production export time limit. Reports isolate temporary preview storage/preferences and do not modify the supplied project.

## Validation limits

The implementation was exercised on an eight-core Apple silicon Mac, with simulated workstation resource inputs, CPU compositor comparisons, and hardware-failure fixtures. Actual Threadripper and NVIDIA throughput, long-run memory behavior, and device output still need validation on that hardware. The optional CUDA integration test is enabled with `ISAAC_RUN_CUDA_ACCEPTANCE=1` when running `gpu_pipeline_test`; it checks native/iPhone boundary-frame output against MLT. A skipped hardware test is not an acceleration success.

Use the benchmark matrix and a long representative project on the target rig, checking preview responsiveness during preparation/export, steady-state memory, visual output, audio synchronization, cancellation, and saving the optimized output to iPhone Photos. Unsupported effects continue through the MLT renderer. Filling all RAM or keeping every engine busy is not itself the goal.

See [implementation measurements](performance-implementation-results.json) for local before-and-after results and [the original audit](performance-audit.md) for the baseline findings.

## Earlier high-priority before and after measurements

The final Release build passed all 30 CTest targets in 155.50 seconds and all 17 packaging checks. Twenty-four actual application-worker exports, with identical frozen projects and software encoding, each decoded to exactly 300 video frames at 1080p60 with AAC audio. Builds and tests were stopped during timing.

| Five-second workload | Original median | Updated median |
| --- | ---: | ---: |
| Plain video and audio | 1.78 s | 1.72 s |
| Title, transform, image overlay | 7.02 s | 7.23 s |
| Title, transform, video overlay | 9.71 s | 9.36 s |
| Overlay visible for only the last frame | 4.17 s | 1.72 s |

The inactive-overlay case used 58.7% less elapsed time. Small differences in the other medians are not a dependable speedup or regression. On this eight-core 8 GiB host, available RAM usually stayed below the 2 GiB threshold and selected one processing worker. One video-overlay run with enough free memory selected four workers and finished in 4.12 seconds. Its updated run range was 4.12–9.57 seconds, so the median must not be extrapolated to a workstation with ample RAM.

One complete decoded-output pair for each plain and continuously active composition workload matched the original pixel for pixel. Inactive-overlay output is checked against the correct plain-video color path, because the original renderer performed an unnecessary RGBA round trip even when nothing was visible. Separate regressions cover source replacement, cache pressure, corrupt copies, and hardware-driver failure.

Current follow-up verification and measurements are recorded in [remaining performance results](performance-remaining-results.md).
