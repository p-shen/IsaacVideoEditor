# Editing and export performance audit

October 31, 2026 · Source revision `75e21a762245ddcb33145ad501961fc3285ac1ae`

The high-priority follow-up is implemented. See [Workstation performance](performance.md) for current behavior and [implementation measurements](performance-implementation-results.json) for verification. The findings below describe the starting revision named above.

IsaacVideoEditor can benefit substantially from a workstation with more cores, an NVIDIA GPU, and 168 GB of RAM, but the current application does not adapt its video workflows to that hardware. The clearest gaps are missing NVIDIA encoding, a fixed MLT frame-processing setting, serial background queues, and small fixed editing caches. FFmpeg already uses multiple CPU cores; the application is not entirely single threaded.

On the available eight-logical-core Apple silicon Mac, controlled software exports with four MLT processing workers took about half the time of the current setting for a title, transform, and image overlay, and about 42% of the time for the corresponding video overlay. Plain footage showed no useful gain. These are local workload comparisons, not predictions for a Threadripper or RTX 4070 Ti.

The original audit changed documentation only. Its baseline measurements and verification details are in [performance-audit-results.json](performance-audit-results.json).

## Findings by priority

### 1 NVIDIA video acceleration is missing

**High priority · Editing and export · Confirmed in source**

[Export encoder selection](../src/mlt_engine.cpp#L433) offers `h264_videotoolbox` on macOS and `libx264` otherwise. The hardware probe is inside `Q_OS_MACOS`. Windows and Linux therefore always select software H.264, even when a 4070 Ti and suitable drivers are present. [Proxy generation](../src/preview_cache.cpp#L379) also always uses libx264. No application code selects NVIDIA decoding or GPU scaling.

Add a tested `h264_nvenc` path on Windows and Linux, initially for export and then for suitable proxy jobs. Probe the encoder exposed by the actual MLT/FFmpeg runtime and test a short render; an encoder name listed by a separate FFmpeg executable does not prove that the application's MLT plugin can use it. Preserve the current software fallback, output validation, isolated worker, cancellation, and destination protection. Encoder quality settings need visual calibration against the existing output.

This change must also reach [worker crash retry detection](../src/export_job.cpp#L98) and [export status text](../src/editor_jobs.cpp#L152), which currently recognize only the Apple encoder as hardware. They would misclassify a newly added NVIDIA encoder without corresponding changes.

NVIDIA distinguishes NVENC encoding, NVDEC decoding, and GPU scaling. Enabling NVENC alone leaves CPU decoding and compositing in place. A later decode/scale path should select supported source formats and retain fallback for unsupported ones. [NVIDIA FFmpeg guidance](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/ffmpeg-with-nvidia-gpu/index.html) documents these separate stages and the importance of avoiding unnecessary transfers between CPU and GPU memory.

### 2 Export processing does not scale with available cores

**High priority · Export · Confirmed and measured**

[Export](../src/mlt_engine.cpp#L460) fixes `real_time` at `-1`. This selects the single-worker MLT processing path while preserving every frame. It is separate from codec threading. The current consumer's `threads` property was zero in the benchmark, and the installed MLT implementation passes zero through to FFmpeg's automatic codec selection. Forcing one encoder thread made the plain sample substantially slower.

| Five-second 1080p60 workload | Current processing setting | Four processing workers | Result |
| --- | ---: | ---: | --- |
| Plain video and audio | 1.38 s | 1.42 s | No useful gain |
| Title, scaled main clip, image overlay | 6.40 s | 3.17 s | 50.5% less elapsed time |
| Title, scaled main clip, video overlay | 8.75 s | 3.65 s | 58.3% less elapsed time |

Each value is the median of three runs. One- versus four-worker decoded video hashes matched exactly for one complete output pair from each workload. All benchmark outputs were decoded to check their frame counts.

More workers did not keep improving performance: eight took 3.43 s for the image composition versus 3.17 s with four, while median peak resident memory rose from 710 MiB to 961 MiB. In the plain sample, automatic encoder threading took 1.38 s, eight explicit encoder threads took 1.40 s, and one encoder thread took 3.96 s.

Benchmark a bounded set of processing-worker counts on representative projects and supported hardware. Keep negative `real_time` values for export so frames cannot be dropped. Account separately for rendering, decoder and encoder threads, concurrent preview, and background jobs. Keep codec automatic selection available; setting every stage to the total logical-core count can oversubscribe the machine. [MLT consumer documentation](https://www.mltframework.org/plugins/ConsumerAvformat/) explains the processing setting; the [MLT 7.42 encoder implementation](https://github.com/mltframework/mlt/blob/v7.42.0/src/modules/avformat/consumer_avformat.c#L878) establishes the codec-thread behavior used here.

### 3 Background preparation runs through serial queues

**High priority · Editing · Confirmed in source**

[MediaCache](../src/media_cache.cpp#L231) uses one process for both thumbnails and waveforms. Each thumbnail launches a fresh FFmpeg process and decodes one image. A long waveform job can hold that process while urgent thumbnails wait. Nonpriority work stops starting during interaction. [PreviewCache](../src/preview_cache.cpp#L337) independently uses one process for proxy preparation. Its software codec may use multiple threads, but separate source jobs still run serially.

[Export startup](../src/editor_jobs.cpp#L147) kills and requeues active proxy preparation and suppresses ordinary media-cache work regardless of machine capacity. A workstation can therefore have spare resources while the editor waits for preparation. Proxy priority changes can also discard partially completed work.

Use a small shared resource budget with separate interactive and background priorities. Start by comparing two and four background jobs on the workstation, with explicit thread and memory allowances per job. Separate long waveform work from urgent thumbnails, and batch nearby thumbnail requests by source to reduce process and decoder startup. Preserve current source fingerprints, stale-result rejection, cancellation, viewport prioritization, and queue bounds. Allow preparation alongside export when measured spare capacity permits it.

### 4 Additional RAM has little application managed editing work to cache

**High priority · Editing · Confirmed in source**

[Thumbnail storage is fixed at 16 MiB and waveform storage at 8 MiB](../src/media_cache.h#L58). The [text layout cache](../src/text_renderer.cpp#L54) uses an 8 MiB conservative complexity budget; that figure is not an exact cap on Qt's allocations. None of these budgets changes with physical or available RAM. There is no application-managed cache of decoded or composited playback frames around the playhead.

Preview is also capped at [960 by 540 in the controller](../src/editor_controller.cpp#L665), [960 by 540 in the preview renderer](../src/preview.cpp#L9), and [960 by 540 in proxy generation](../src/preview_cache.cpp#L376). Increasing only one of these limits would not provide a consistent higher-quality preview. The [source viewer](../src/editor_controller.cpp#L834) opens original media rather than using the timeline's ready proxy selection.

Introduce bounded, memory-aware caches and a coordinated preview quality policy. Cache recently decoded or composited frames around the playhead, invalidate them by edit revision and source fingerprint, and prioritize the next likely seek or playback frames. Let a stronger machine use 1080p or higher preview when it can meet the frame deadline. Allow the source viewer to benefit from suitable preview copies too.

For a 168 GB workstation, an **illustrative experiment** is an 8–16 GiB playback cache, a 256 MiB thumbnail cache, and a 64 MiB waveform cache, with memory-pressure backoff. These are starting points to measure, not selected defaults. One uncompressed RGBA frame costs approximately 1.98 MiB at 540p, 7.91 MiB at 1080p, or 31.64 MiB at 4K. A 16 GiB frame budget holds about 34 seconds of 1080p60 or nine seconds of 4K60 before other overhead. Reserve memory for the editor, export worker, decoders, GPU staging, and the operating system.

Extra RAM can already help the operating system cache media files. Application caches are needed to reuse expensive decoding and compositing work. Filling RAM by itself is not a performance objective.

### 5 CPU compositing and image copies limit GPU benefits

**High priority for complex edits · Editing and export · Confirmed in source**

[Transforms](../src/visual_renderer.cpp#L38) request writable RGBA pixels, copy content, clear a frame, and paint with QPainter. [Video overlays](../src/visual_renderer.cpp#L51) hold one mutex across overlay decoding and painting. [Text painting](../src/text_renderer.cpp#L94) holds the global layout-cache mutex while drawing each active text item. These shared operations limit the benefit available from additional frame workers even though the measured compositions still benefited from four workers.

[Preview delivery](../src/preview.cpp#L12) copies RGBA pixels into a QImage, and [VideoPreview](../src/preview_item.cpp#L22) paints through QQuickPaintedItem. Qt can use the GPU to present the interface, but this is not a GPU-resident video pipeline. Qt documents that the default image target is painted into a QImage and then uploaded to a texture. [Qt QQuickPaintedItem documentation](https://doc.qt.io/qt-6/qquickpainteditem.html#renderTarget-prop)

Reduce full-frame conversions and copies, reuse immutable overlay assets, and isolate mutable decoder state before expanding concurrency. Investigate per-worker layout resources or cached text tiles without sharing a mutable QTextLayout unsafely. A scene-graph video texture and GPU transform/composite path are longer-term options if profiling still identifies these stages as the bottleneck. NVENC support can be delivered without replacing the renderer.

### 6 Inactive overlays still force expensive frame conversion

**High priority candidate · Editing and export · Measured with a quality caveat**

[overlayProcess](../src/visual_renderer.cpp#L67) installs its image callback for every frame when an overlay filter is present. That callback requests writable RGBA pixels before checking whether an overlay is active. Hidden lanes can also leave an empty overlay state attached to the timeline.

An overlay visible only on the last frame of the five-second sample increased median export time from 1.38 s for plain video to 3.61 s. A temporary diagnostic copy that skipped inactive callbacks took 1.27 s. The diagnostic is outside application source.

All diagnostic outputs retained 300 video frames, but **all three failed exact decoded-pixel equivalence** against the current output. The first comparison's overall SSIM was 0.988841. Both files reported the same BT.709 limited-range metadata. Skipping the RGBA path changes the conversion and encoding inputs; the color and visual consequences require investigation before this can be adopted. The timing establishes avoidable work, not a production-ready optimization. Cover hidden lanes, boundaries, fades, color conversion, and overlapping overlays when evaluating a fix.

### 7 Editing still performs blocking work and repeated scans

**Medium priority · Editing · Confirmed in source; interactive latency not benchmarked**

[Import](../src/editor_controller.cpp#L333) probes media serially on the interface thread. [Timeline rebuilding](../src/editor_controller.cpp#L647) builds the playback graph synchronously, decodes a still when connecting it, and then seeks and decodes another still at the desired position. [Preview::pause and seek](../src/preview.cpp#L145) also contain synchronous rendering; waiting for a pending seek worker can block the caller. More idle cores do not remove these interface-thread waits.

Playback emits broad state and composition notifications. [currentSource](../src/editor_controller.cpp#L811) constructs the entire assets list to return one item; [assets](../src/editor_controller.cpp#L785) scans clips and overlays for each asset. [Timeline viewport refresh](../src/quick_timeline.cpp#L363) repeatedly computes clip starts, and [clipStartFrame](../src/project.cpp#L152) scans preceding clips. Transition calculations can introduce further scans. Large projects consequently repeat work unrelated to the currently displayed frame.

Move probing and graph preparation to bounded workers with independent producer ownership and revision checks. Connect a prepared graph and request one asynchronous still at the intended position. Narrow notifications, construct the selected-source record directly, and cache clip boundaries and source usage when an edit changes them. Use those boundaries to locate visible clips and the current clip efficiently.

### 8 Cache storage and source preparation need explicit budgets

**Medium priority · Editing and export · Confirmed in source**

[Still overlays](../src/visual_renderer.cpp#L89) are decoded at full resolution when a timeline is built, including future overlays and repeated references to the same image. Editing and export have separate timelines. Share immutable decoded images within each process by source identity and load them on demand under a byte budget.

[Proxy timing checks](../src/preview_cache.cpp#L345) request timestamps for the entire source before some range jobs, bounded by a ten-second timer and a 16 MiB response limit. Long or variable-cadence sources can fall back to preparation from time zero to preserve timing. Retain that correctness rule while investigating reusable timing indexes or a safe bounded probe.

Completed proxy files have no application disk quota or LRU cleanup. Retaining fewer active requests does not remove old completed files. Add a storage budget and eviction of unused copies, with an optional fast local cache location. Disk throughput and cache hit rates should be measured alongside CPU and memory use.

## Recommended order

1. Add performance diagnostics and benchmark controls, then validate workload-sensitive MLT worker counts. Record selected encoder, stage timing, frames per second, peak memory, cache hits, and fallback reasons.
2. Add NVIDIA export encoding and update hardware detection, retry handling, and status text together. Validate on the actual Windows or Linux workstation.
3. Introduce bounded background concurrency and memory-aware editing caches. Coordinate preview quality across the controller, renderer, proxies, and source viewer.
4. Remove interface-thread probing, redundant still decoding, and repeated timeline/model scans.
5. Resolve the inactive-overlay color difference, then evaluate conversion bypasses and shared immutable assets. Profile shared renderer locks before expanding parallelism further.
6. Evaluate GPU-resident decoding, scaling, and compositing where measurements justify the larger change.

The existing isolated export worker, frozen project revision, source-media export, atomic publication, and frame completeness checks provide a useful foundation and should be preserved.

## Verification and workstation acceptance

The Release core, UI, application worker, and five focused test targets built successfully. `preview_cache_test`, `preview_profile_test`, `media_cache_test`, `preview_async_test`, and `export_job_test` passed: **5 of 5 targets in 36.60 seconds**. These checks exercise existing behavior; they do not establish performance on NVIDIA hardware.

Measurements used the current core and compositors with a temporary avformat consumer harness matching the software export settings: H.264 `veryfast`, CRF 20, AAC, 1080p60, and faststart. Timings cover graph construction through consumer completion; they exclude process startup, the normal hardware probe, final validation, and publication. There were 36 exports using current rendering code and three diagnostic bypass exports, with three runs per configuration. Every output decoded to exactly 300 video frames. The source was synthetic moving H.264 video with AAC audio. Short runs and ordinary desktop scheduling introduce noise; some late control runs overlapped diagnostic harness compilation, so small timing differences should not guide defaults.

The code map was used for navigation and findings were checked against source. Its C++ parser reported partial coverage of several headers; the audit does not rely on inferred graph relationships. Earlier measurements in [export-investigation.md](export-investigation.md) used different workloads and hardware encoding, so their timings are not directly comparable.

Before claiming workstation scaling, measure the same representative projects on the intended Threadripper and 4070 Ti, documenting the operating system, core count, available RAM, drivers, bundled codec support, and media/cache/output drives. Include H.264 and HEVC sources, 1080p and 4K, cuts, transitions, dense captions, and multiple video overlays. Compare cold and warm editing caches, source and timeline scrubbing latency, dropped preview frames, background preparation, and editing during export. Sweep worker and cache budgets, preserving visual output, exact frame count, audio synchronization, and cancellation behavior. Run an extended export to verify that memory reaches a bounded steady state.

Measure NVIDIA's video encode/decode engines separately from its 3D engine. Successful scaling means shorter exports, faster preparation, and responsive high-quality preview with bounded memory. It does not require every CPU core, the GPU, and all RAM to remain fully occupied at once.
