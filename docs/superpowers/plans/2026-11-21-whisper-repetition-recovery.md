# Whisper Repetition Recovery Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans. Continue the reported subtitle defect inline with focused regressions and one final independent review.

**Goal:** Recover from the reported long-recording repetition loop without publishing a known looping transcript as successful.

**Architecture:** Keep the existing audio render, model and worker. Detect conservative, long consecutive runs of the same normalized phrase in validated Whisper output. Retry once with `-mc 0` to disable previous-text context between internal decoding windows; if repetition remains, fail safely and preserve the destination. Keep the original timeline offsets and warn when recovery succeeds.

**Tech Stack:** C++17, Qt, existing whisper.cpp CLI and Qt Test workers.

## Evidence and choices

The Windows log shows successful CUDA initialization, 463.1 seconds of audio and normal process completion. The same seven-character Mandarin phrase repeats 74 times from 311.740 to 459.740 seconds. The user reports a roughly five-second quiet interval with background noise before the loop. The trigger is plausible but has not been reproduced from the actual audio on this Mac.

Resetting previous-text context on a single bounded retry is the smallest compatible recovery. External VAD adds another model and speech-boundary behavior; manual file chunking adds timestamp/seam handling and model reloads. Defer those until evidence shows the bounded retry is insufficient. Do not delete repeated captions to disguise failure.

## Constraints and review focus

- Preserve original audio, 60 fps timing, UTF-8 text, output replacement safety and controller/import policy.
- Detect at least eight adjacent repetitions of a phrase containing at least six letters/numbers, spanning at least fifteen seconds; gaps over one second reset the run.
- Ignore punctuation/case differences for detection, but preserve the actual caption text.
- Keep ordinary short repetitions and repeated phrases separated by meaningful gaps.
- Retry once, remove only the private first-pass JSON before retry, reset GPU confirmation/progress and support cancellation throughout.
- Do not publish a retry that crashes, produces no output, or still loops. No claim of Windows hardware or real-clip recovery without execution there.

## Additional evidence: Turbo and audio preparation

The second user log uses the same 7,409,067 audio samples. Turbo resumes changing dialogue where Large-v3 loops, but includes 18 instantaneous fragments and fails the old generic timestamp check. Omit zero-duration decoder fragments with an explicit review warning; continue rejecting reversed/out-of-bounds intervals and all-empty output. Add a 463.1-second synthetic audio-render check with quiet background noise at 310–315 seconds and a changed tone afterward. These checks do not establish the actual Windows prepared WAV is correct.

## Task 1: Safe repetition recovery

**Files:** `src/subtitle_job.h`, `src/subtitle_job.cpp`, `tests/subtitle_job_test.cpp`, `docs/captions.md`.

**Interfaces:** Extend `Subtitles::whisperSrt` with an optional `int *repetitionStartMs` output, initialized to -1. Existing callers remain compatible. `SubtitleJob` owns a per-job retry timestamp and uses it to bound retries and choose `-mc 0`.

- [x] Add a regression reproducing the supplied 74-cue timing pattern, plus accepted short/spaced repetitions.
- [x] Add worker regressions for successful recovery, persistent loops, retry crash/no output, and retry cancellation with destination preservation.
- [x] Observe failures, then implement detection and one context-free retry.
- [x] Run focused tests and a real local CLI check that the context option preserves absolute timestamps.
- [x] Review the patch once; fix material findings, rebuild the app and run the affected/full suite as appropriate.
- [x] Document outcomes and platform/actual-audio limits; keep publication separate unless requested for this fix.

## Verification results

- Full Release build succeeded; 39/39 CTest targets passed in 240.67 seconds on macOS. The normal development app was rebuilt.
- Tests cover the supplied 74-cue timing pattern, accepted short/spaced repetitions, bounded recovery, persistent loops, crash/missing retry output, cancellation, model changes before retry, instantaneous fragments and strict invalid-timestamp rejection.
- A controlled 463.1-second audio render preserves a quiet/noisy five-second interval and subsequent changed audio through the end. A real Turbo CLI invocation with `-mc 0` produced nine valid segments on the existing short local test clip.
- One independent review found no Critical/Important issues; its duplicate-include finding was fixed. The later Turbo evidence and model-change guard have focused regressions.
- Actual Windows CUDA recovery, original source track metadata and the real prepared WAV remain unverified. Changes are local and uncommitted; no push performed for this defect investigation.
