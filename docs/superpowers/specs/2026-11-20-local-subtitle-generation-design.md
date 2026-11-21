# Local subtitle generation

Status: implemented and verified on macOS on November 20, 2026. Windows/NVIDIA and Linux execution remain pending. Implementation and verification are recorded in the accompanying plan.

## Intent and decisions

Generate a UTF-8 `.srt` from the edited timeline inside IsaacVideoEditor. The user selected timeline audio and subsequently identified the main target as Windows on a Threadripper with an NVIDIA RTX 4070 Ti. They requested model switching and a larger Whisper model. Local processing and avoiding paid transcription providers remain requirements. English and Mandarin must remain in their original languages, with Simplified Chinese output.

The user preferred Qwen to the earlier `large-v3-turbo-q5_0` Whisper trial on one short clip. This does not establish an accuracy ranking against full Whisper large-v3. The existing personal Qwen skill and its models remain usable and unchanged. The first application module proposed here offers two Whisper models; integrating Qwen into the application is outside this first version.

## Approaches

Proposed implementation choice: run a pinned native `whisper.cpp` executable as a separate process. It supports full large-v3 and large-v3-turbo, Windows CUDA, and macOS Metal/CPU. A separate executable avoids connecting the editor's MinGW Qt build to a different compiler's CUDA libraries and contains inference failures. Both choices use the same generation and import path.

`faster-whisper` also supports NVIDIA inference and word timestamps, but would add a Python environment and CTranslate2/cuDNN deployment to the application. The existing Qwen/MLX runner has already been tested on the Mac, but does not provide the Windows NVIDIA runtime requested here. Native Whisper is the proposed first application backend; no general provider/plugin framework is needed.

## User flow

Extend the existing Captions panel with **Generate SRT**, a model selector, and a compact status area. Models are **Whisper large-v3 — accuracy** and **Whisper Turbo — speed**. These labels express the intended tradeoff, not a promise about any particular recording. Remember an explicit model choice per installation. Recommend full large-v3 on the Windows NVIDIA target; use Turbo as the initial Mac choice to reduce memory pressure. Expose the actual processing device and any CPU fallback in ordinary language.

The first run offers to download the selected model, showing its size and allowing cancellation. Store verified weights in application data, independently of saved projects and source media. Use fixed, pinned model identifiers and trusted download locations; verify the expected digest before making a model available. Reuse an already installed matching model when found in the app's supported model directory. A partial or failed download must never replace a valid model. Inference works offline after setup; no audio or transcript is uploaded.

Generate opens a save dialog for the `.srt`, captures the current committed project revision, and starts a background job. Show **Preparing audio**, **Transcribing**, and **Saving subtitles**, with progress when the worker supplies it and an indeterminate indicator otherwise. Cancel remains available during every stage. Empty timelines and unavailable engines give a useful message before audio rendering.

On success show the output path, model/device actually used, and timing warnings. Offer **Import generated captions** and **Show file**. Do not automatically modify the project. Import uses the existing append behavior and controller/Undo path, and warns when existing captions would be duplicated. Subsequent text corrections, selected-word color/bold, emoji and SRT export use the existing caption controls.

## Timeline and job behavior

Use original media and the captured project's complete audible mix, including clip trims, ordering, transitions, mute/volume/fades, voiceover and music. The output clock starts at project time zero. Reuse `GpuPipeline::renderAudio` and the shared MLT timeline builder to prepare mono 16 kHz PCM WAV in a job-owned temporary directory. Extend its output handling without changing the AAC path used by GPU video exports. Do not encode a temporary video.

A new subtitle job coordinates an isolated app audio-render worker and then the native Whisper process through asynchronous `QProcess`. Resolve the bundled engine first. Developer overrides must be explicit; production should not silently run an unrelated executable found on PATH. Use argument arrays and fixed staging filenames. Limit captured logs/protocol buffers. Engine stdout/stderr is diagnostic input, never application instructions.

Only one export or subtitle-generation job runs at once. Pause playback on start and suspend competing media preparation using the existing resource-budget patterns. Keep the interface responsive and allow editing. Cancellation stops the active process, with a bounded grace period followed by termination, then cleans only that job's temporary files. Closing the app accounts for the new job. Restore background preparation on every success, failure and cancellation path.

Record the originating project/revision and media identities. If the project, timeline or source media changes while generation runs, retain a valid completed SRT as a result for the captured revision, label it accordingly, and disable the one-click import into the changed project. The user can regenerate or manually import after review. A source change that invalidates the captured audio makes the run fail without publishing the result. Conservative revision invalidation is acceptable for this first version; false claims of current timing are not.

## Transcript, validation and publishing

Request transcription and language detection, not translation. Normalize Chinese script without changing English or inventing words. Use actual model segment timestamps; Whisper's segment/token estimates must not be described as Qwen forced alignment or verified word timing. There is no forced-aligner step in this version and no Qwen five-minute alignment limit.

Parse the worker output before publication, using the existing subtitle parser and 60 fps conversion. Preserve literal text/entities and validate numbering, finite positive intervals, ordering, cue/text/file limits and media bounds. Keep every recognized phrase. Flag brief intervals and overlaps for review. Do not silently stretch, discard or claim accurate timing for uncertain cues. An invalid, truncated or failed worker result cannot become a successful SRT.

If the worker recognizes no text, say so and preserve the existing destination; do not invent captions or report a new populated SRT.

Protect all source, asset, audio and saved-project paths from output collisions. Stage output privately and publish it atomically only after validation. Replacing an existing SRT requires the save dialog's overwrite decision; failure and cancellation preserve its previous bytes. Generation itself does not dirty the project. Import is one undoable edit and never overwrites existing styled captions automatically.

## Packaging and platform boundaries

Ship a pinned engine and its required runtime libraries with the relevant application package; model weights are a separate download. Windows CUDA worker builds/distribution must be independent of the editor's MinGW compiler. Package license notices and include native worker/runtime discovery in packaging checks. Do not assume the existing FFmpeg NVENC support proves Whisper CUDA is available. Verify the selected inference backend actually initializes; report a missing/incompatible GPU runtime and offer an explicit CPU route instead of silently labelling CPU work as GPU work.

Windows with the RTX 4070 Ti is the main acceptance target. macOS remains useful for development, CPU/Metal smoke testing, and existing editing workflows. Other existing platforms must continue to build; unavailable transcription engines must not disable caption import/editing. Full model downloads and CUDA setup need platform-specific package verification. No Windows GPU is available in this Mac workspace, so NVIDIA acceptance must be reported as pending until exercised on that machine or equivalent CI hardware.

## Verification and builder choices

Test the job lifecycle with deterministic worker fixtures: success, no recognized text, startup failure, malformed/truncated results, crash, cancellation during each stage, destination preservation, source collisions, repeated generation and app close. Test stale results, optional import/Undo, duplicate warnings, language/script handling, Unicode/emoji, non-ASCII Windows source/destination/user-profile paths, subtitle limits and frame round-trips.

Use synthetic timeline fixtures with distinguishable audio on both sides of a cut, a transition, voiceover and mute/volume settings to verify rendered WAV duration and sample content against the captured mix. Check keyboard operation, save-dialog cancellation, progress, warnings and import in the running app. Run the affected checks and full CTest suite because shared audio rendering is involved. Run applicable packaging helpers and packaged-runtime checks.

Finally compare full large-v3 and Turbo on the user's existing short clip without overwriting either earlier transcript. Treat the user's listening review as the accuracy decision. Keep the verified Qwen comparison available. Check a longer input to ensure no accidental five-minute restriction remains.

The builder may settle class/file names, exact compact layout within existing controls, progress event parsing, native Simplified Chinese conversion, pinned engine/model revisions and hashes, and platform build steps. Preserve existing project formats; no transcription metadata, model weights or caches belong in saved project files.

## Sources checked

- [whisper.cpp platforms, models and CUDA](https://github.com/ggml-org/whisper.cpp)
- [whisper.cpp v1.9.5 release](https://github.com/ggml-org/whisper.cpp/releases/tag/v1.9.5)
- [OpenAI's Turbo model explanation](https://github.com/openai/whisper/discussions/2363)
- [faster-whisper runtime and deployment requirements](https://github.com/SYSTRAN/faster-whisper)
