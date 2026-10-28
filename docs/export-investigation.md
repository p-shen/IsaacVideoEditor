# Export performance and crash investigation

October 27, 2026

Meaningful improvements are available within the existing Qt and MLT renderer. The strongest measured speed improvements are bypassing inactive overlay compositing and caching static text. Running export in a separate process should contain native renderer crashes so they do not close the editor. The cause of the reported whole-app crash on an 18-minute 1080p export remains unconfirmed.

The initial figures below come from temporary diagnostic builds; their measurements are saved in [export-investigation-results.json](export-investigation-results.json).

## Implemented changes

The approved follow-up isolates export in a child process, keeps staging and final publication under editor ownership, retries a hardware-worker crash once with software, and exposes a bounded diagnostic log in the export panel. Cancellation, failed startup, invalid worker output, worker crashes, and frozen-revision retries preserve source media and existing output. The worker loads the same bundled font as the editor.

Text now uses a bounded layout cache with an 8 MiB conservative complexity budget and at most 128 recent entries. It preserves the original per-primitive painting and fades. Actual five-second 1080p60 software exports with dense Unicode captions improved about 9.7%; short two-line titles showed no demonstrated speed gain. All decoded frames matched the uncached renderer byte for byte. These production results differ from the earlier raster-cache prototype's 31% improvement. Raw cache measurements are saved in [export-implementation-results.json](export-implementation-results.json).

The unused-overlay bypass, lazy image loading, and queue-size changes remain investigation proposals outside the implemented cuts/audio/text work. The reported 18-minute project crash still lacks a source-media reproduction; worker isolation limits the effect of a native renderer crash while diagnostics help identify it.

## Original export architecture

Before the follow-up implementation, `EditorController::startExport` in `src/editor_jobs.cpp` froze a project snapshot and called `Media::exportMp4` from a `QThread` inside the editor process. Preview preparation is paused, and edits can continue. A thread isolates work scheduling but cannot contain a segmentation fault, abort, or fatal native library error.

`exportMp4` in `src/mlt_engine.cpp` renders original media at the project canvas size and 60 fps. On macOS it probes VideoToolbox, tries hardware H.264, validates output, and retries the entire render with libx264 when the hardware attempt returns failure. That retry cannot recover from a process crash. Progress and the export details do not identify the selected encoder or explain a fallback.

The existing temporary-file publication and destination-preservation behavior should be retained. A worker process can reuse this renderer and the saved project format, including custom text and visual filters.

## Measured speed improvements

Tests used moving 1920×1080 H.264 video, AAC audio, a Release build, Qt 6.12.0, MLT 7.42.0, and FFmpeg 9.0.2 on an eight-core Apple silicon Mac. Hardware encoding was available when the benchmarks ran with normal hardware access. Each comparison below exports five seconds at 60 fps. These are individual synthetic runs, so ratios indicate opportunities rather than predicted improvements for the reported project.

| Workload | Current behavior | Diagnostic change | Reduction in elapsed time |
| --- | ---: | ---: | ---: |
| Overlay outside the visible sequence | 7.80 s | 2.81 s | 64% |
| Hidden overlay lane | 6.39 s | 3.30 s | 48% |
| Overlay visible for the first second | 5.72 s | 3.37 s | 41% |
| Static title throughout | 12.48 s | 8.63 s | 31% |

`VisualRendering::overlayProcess` always attaches `overlayImage` whenever a project has visual overlays. `overlayImage` requests a writable RGBA image before checking whether any overlay is visible at that frame. Hidden lanes can produce an empty overlay state, and overlays outside the sequence can also leave no active sources. Both cases still force conversion of the complete video frame. The diagnostic change checked active intervals before requesting the image.

`TextRendering::paint` creates and lays out text on every active frame. A diagnostic cache rendered static text into a small transparent image once and applied its opacity during composition. A cropped sample of the encoded title had SSIM 0.999777 compared with the existing output; complete coverage of fades, fonts, scaling, edits, and cache invalidation is still required before adopting this implementation.

Keeping the RGBA conversion but omitting text drawing took 6.38 seconds in the same five-second workload. Text layout and rasterization therefore account for a substantial part of the title cost, while conversion of the complete frame remains another important cost.

## Memory and stability

Visible still-image overlays are decoded at full resolution and retained in `OverlayState::sources` when the timeline is constructed. This happens for future overlays as well as currently active ones, and repeated references to the same image are decoded separately.

A preparation-only experiment measured peak resident memory before rendering the first frame:

| One-frame 4K PNG overlays | Peak memory |
| --- | ---: |
| None | 65 MiB |
| One | 91 MiB |
| Twelve referencing the same image | 330 MiB |

Preview and export use separate timelines. Projects with many large images can therefore hold substantial duplicate image data. This is a confirmed resource issue and a plausible contributor to memory pressure; it does not establish the cause of the reported crash. Loading stills when needed and using a bounded cache keyed by source identity would address the growth while preserving full-resolution output. Image scaling also needs to respect crop and zoom requirements.

The exporter leaves MLT's output queue at its default of 25 frames. Reducing the queue to four lowered peak RSS from 333 MiB to 253 MiB on a ten-second plain export, with elapsed time changing from 4.63 to 4.36 seconds. The heavier text, transform, and video-overlay case was slower with that smaller queue: 22.99 versus 33.89 seconds. Queue size needs workload testing before selecting a new default. Faster frame preparation can also fill the queue more readily, so bypassing compositing by itself does not guarantee lower memory.

Increasing MLT processing threads from one to four did not show a dependable gain. In the heavy ten-second composition case it took 49.62 seconds versus 22.99 seconds with the existing setting. MLT distinguishes frame-processing threads from codec threads; the current encoder thread property was zero, allowing codec defaults. The pipeline should not be described as having only one thread in total. See [MLT consumer settings](https://www.mltframework.org/plugins/ConsumerAvformat/) and [MLT 7.42 encoder implementation](https://github.com/mltframework/mlt/blob/v7.42.0/src/modules/avformat/consumer_avformat.c).

## Recommended implementation order

1. **Contain export crashes and capture diagnostics.** Run the existing renderer in a child process using the frozen project snapshot. Report encoder choice, fallback reason, last frame, and native errors. Detect worker failure without closing the editor. Preserve cancellation, safe destination replacement, and ownership of temporary output. Retry with software after a hardware worker crash when appropriate. This limits the effect of a renderer crash; it does not cure its underlying cause or a system-wide memory shortage.
2. **Bypass inactive overlays.** Check visibility and time ranges before requesting a writable RGBA frame. Cover hidden lanes, future and expired overlays, overlapping lanes, and exact start/end boundaries.
3. **Bound image residency.** Decode stills on demand, reuse repeated source images, and release inactive images through a bounded cache. Track memory while preview and export coexist.
4. **Cache static text.** Reuse rendered text tiles and apply fades during composition. Bound the cache for projects with many captions and invalidate it when text, style, position, or canvas size changes.
5. **Tune buffering using representative projects.** Compare queue limits on cuts, transitions, captions, high-resolution sources, and video overlays. Retain frame completeness and image quality.

No renderer replacement or frame-rate reduction is needed to pursue these improvements.

## Initial investigation verification and remaining reproduction

All 22 isolated exports completed, including the diagnostic cases. The normal hardware path verified exact video sample counts. A one-minute 1080p export using inactive-overlay bypass and a four-frame queue completed with all 3,600 video frames. Its peak RSS was about 431 MiB and remained stable from the first 10% progress sample through completion. This is shorter than the reported 18-minute workload.

Existing focused export tests passed with normal hardware access: hardware selection, deliberate incomplete-hardware output followed by software fallback, export dimensions, failure preserving an existing destination, and successful replacement. The recovery/export, text-overlay, and composition test executables also passed. An initial broader run encountered a preview-source-reuse test failure while unrelated preview changes were being edited in the shared checkout; this investigation does not claim a passing full suite.

The five available local editor crash reports were Qt platform initialization aborts from temporary package tests, not export crashes. Reproducing the reported failure requires the affected project and media or an export crash report. Full-duration validation should include an 18-minute 1080p export, memory observations, preview activity, cancellation, and a deliberately crashed worker to prove the editor survives.

## Implementation verification

The final integrated Release build passed all 27 CTest targets on macOS with normal encoder and cache access in 131.96 seconds. The application smoke check passed real worker export, replacement of an existing export, and missing-media rejection. Worker tests cover intentional crash recovery, a hardware-worker crash retried with software, startup/render/finalization cancellation, cancellation after publication, malformed protocol output, bounded logs, source collision protection, and retrying the frozen revision after subsequent edits. A regression reproduced missing bundled-font rendering before the fix and passed after the worker reused the editor's font initialization.

Dense-caption measurements and exact decoded-frame comparisons are recorded in [export-implementation-results.json](export-implementation-results.json). The original 18-minute project and Windows/Linux runtime execution remain outside this validation. The completed changes remain in the existing working checkout on `codex/export-reliability`; unrelated pending edits were preserved.
