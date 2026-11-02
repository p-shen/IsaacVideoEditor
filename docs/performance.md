# Workstation performance

The editor automatically selects bounded CPU and memory budgets. Windows and Linux exports and preview preparation try NVIDIA H.264 encoding when the installed runtime and GPU support it. Macs try VideoToolbox. Failed hardware initialization falls back to software H.264.

## Editing

Recently displayed frames are retained for faster repeated scrubbing. Cache validity includes the producer revision and source path, size, and modification time. Text and visual edits invalidate old frames; replacing a source also retires cached pixels. Memory limits shrink when available RAM decreases.

Capable machines use up to 1080p preview consistently across the timeline, source viewer, and generated preview copies. Smaller machines retain the 540p limit. The source viewer can use a full-source preview copy and retries the original if that copy is damaged. Partial copies remain available to the timeline without changing source-viewer positions or playback length.

Background preparation has independent process and output state for every job. On a workstation it permits two proxy jobs and two thumbnail/waveform jobs, with explicit decoder, encoder, and filter thread limits. Urgent thumbnails can preempt a long waveform. Machines with spare capacity continue preparation during playback and export; conservative budgets defer ordinary interactive work and pause proxy preparation during export.

Photos load when their overlay becomes active. References to the same canonical source identity share decoded pixels across timelines within the process. Active references and in-flight frames retain their pixels, avoiding repeated decoding when the inactive-image cache fills. The inactive-image LRU is bounded at 32 MiB. Distinct simultaneously active images still require their decoded working memory. Text layout caches are bounded per rendering thread, allowing independent frame rendering without a global text-drawing lock.

## Export

Exports retain native canvas resolution by default. The optional **Optimize for iPhone** setting fits the output within a landscape 3840 × 2160 or portrait 2160 × 3840 envelope without upscaling or changing the project. It uses H.264 High at Level 4.2 for HD-sized rasters and Level 5.2 for larger rasters, with peak bitrate limits of 16 or 40 Mbps and a two-second rate-control buffer. NVIDIA, Apple, and software encoders receive the same compatibility policy. Hardware output validation checks the resulting level and pixel format, and retries preserve the selected mode.

This addresses an iPhone 15 Plus Photos import failure with a 2880 × 3840, 60 fps, H.264 High Level 6.0 export at approximately 112 Mbps. The original-format short sample could not be imported; the user confirmed successful imports of 2160 × 2880 Level 5.2 and 1080 × 1440 Level 4.2 samples. These samples used software encoding, so a new optimized NVIDIA export still needs a device check.

Plain edits use one MLT frame-processing worker while FFmpeg retains its automatic codec threading. Compositions can use up to eight frame-processing workers, limited by CPU count and the memory required for the output dimensions. Export always disables frame dropping.

Queues reserve space for working frame copies as well as buffered frames. Their memory allowance is at most one eighth of available RAM, capped at 2 GiB. This permits parallel 4K rendering on a large workstation without forcing large queues onto a smaller computer. Queue estimates do not include every allocation made by codecs or the operating system.

Hardware capability is checked with a short encode through the actual MLT consumer in the isolated worker. A parent-side ten-second deadline handles a driver wedged during probing. A hardware crash or stalled probe gets one software retry. Cancellation suppresses retry. Missing or corrupt overlay images produce an explicit render error so an incomplete composition cannot replace an existing output.

The export log identifies NVIDIA, Apple, or software encoding, the selected worker and queue settings, available memory, fallback reasons, and encoding throughput. Keep this log when comparing machines or reporting an export problem.

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

## Validation limits

The implementation was exercised on an eight-core Apple silicon Mac, with simulated workstation resource inputs and hardware-failure fixtures. Actual Threadripper and NVIDIA throughput and quality still need validation on that hardware. NVENC accelerates encoding; GPU-resident decoding, scaling, and compositing remain future work. Import probing and proxy disk eviction are separate medium-priority audit items.

See [implementation measurements](performance-implementation-results.json) for local before-and-after results and [the original audit](performance-audit.md) for the baseline findings.

## Local before and after measurements

The final Release build passed all 30 CTest targets in 155.50 seconds and all 17 packaging checks. Twenty-four actual application-worker exports, with identical frozen projects and software encoding, each decoded to exactly 300 video frames at 1080p60 with AAC audio. Builds and tests were stopped during timing.

| Five-second workload | Original median | Updated median |
| --- | ---: | ---: |
| Plain video and audio | 1.78 s | 1.72 s |
| Title, transform, image overlay | 7.02 s | 7.23 s |
| Title, transform, video overlay | 9.71 s | 9.36 s |
| Overlay visible for only the last frame | 4.17 s | 1.72 s |

The inactive-overlay case used 58.7% less elapsed time. Small differences in the other medians are not a dependable speedup or regression. On this eight-core 8 GiB host, available RAM usually stayed below the 2 GiB threshold and selected one processing worker. One video-overlay run with enough free memory selected four workers and finished in 4.12 seconds. Its updated run range was 4.12–9.57 seconds, so the median must not be extrapolated to a workstation with ample RAM.

One complete decoded-output pair for each plain and continuously active composition workload matched the original pixel for pixel. Inactive-overlay output is checked against the correct plain-video color path, because the original renderer performed an unnecessary RGBA round trip even when nothing was visible. Separate regressions cover source replacement, cache pressure, corrupt copies, and hardware-driver failure.
