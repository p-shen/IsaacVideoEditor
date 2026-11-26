# Whisper Word Timing Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans. The user approved the built-in Whisper timing approach and waveform review with “yes”. Execute inline and preserve the uncommitted recovery fix.

**Goal:** Refine generated caption boundaries with Whisper token timings and make their relation to timeline waveforms visible.

**Architecture:** Request full JSON (`-ojf`) from the existing native worker. Use the first and last meaningful text-token offsets only when the complete segment text is represented by ordered, positive token intervals inside its original segment; preserve segment text, original timeline clock and validation. Retain segment timing with a concise warning when word timing is missing or incomplete. Draw two selected-caption boundary guides through the existing timeline waveform lanes; no new inspector panel or model download.

**Tech Stack:** C++17, Qt Quick painted timeline, existing whisper.cpp and Qt Test.

## Decisions and constraints

- This is Whisper's experimental token timing with native signal-energy refinement, not a guarantee of forced alignment. The user's original Windows audio remains unavailable here.
- Use the existing waveform lanes for visual review; their cached source peaks are not a speech detector or a rendered-mix measurement.
- No blanket 0.5–1 second shift. No change to existing captions, project schema, source media, import/Undo or 60 fps timebase.
- Ignore decoder control tokens and punctuation for spoken bounds; require complete text coverage and valid timing for every lexical token before trimming.
- Missing, zero-width, out-of-order, nonfinite or out-of-segment word timing falls back with a review warning. Invalid segment timing still fails safely.
- Keep repetition detection based on original segment intervals so refinement cannot hide a long loop. Preserve existing bounded retries and cancellation.
- Draw guides only for the primary selected caption, using the existing source-to-project timing mapping and viewport clip; avoid per-frame scans over all captions.

## Task 1: Generated word timing

**Files:** `src/subtitle_job.cpp`, `tests/subtitle_job_test.cpp`, `docs/captions.md`.

**Interfaces:** Keep `Subtitles::whisperSrt` and controller interfaces unchanged. Add an internal optional bounds helper and switch the worker flag from `-oj` to `-ojf`.

- [x] Add regressions for English/Mandarin/control tokens, unchanged text, later timeline offsets, incomplete/invalid token fallbacks, and repetition detection with word data.
- [x] Observe RED, implement bounds refinement and full-JSON request, then run the affected target.
- [x] Exercise the real local engine and inspect output timing/format. Preserve existing comparison results.

## Task 2: Waveform review guides

**Files:** `src/quick_timeline.cpp`, `tests/quick_timeline_test.cpp`, `docs/captions.md`.

- [x] Add a rendered regression showing selected-caption edge guides over waveform lanes, clearing with deselection and following zoom/scroll.
- [x] Observe RED, implement the two guides using existing geometry and theme colors, then run timeline checks.
- [x] Inspect the rendered native interface, request one fresh scoped review, rebuild the normal app and run the full CTest suite.
- [x] Record actual checks and remaining Windows/listening limits. Leave changes local unless publication is requested.

## Verification results

- Full CTest suite: 39/39 targets passed in 239.15 seconds on macOS. After the Unicode review fix, the complete subtitle-job target passed all 30 cases. The normal app rebuilt successfully.
- Both cached full models completed real generation through the application/controller on the local 18.5-second clip with Apple GPU, followed by import/Undo. Original video SHA256 remained unchanged. New SRTs preserve all eight cue texts per model; this continuous-speech sample showed small timing refinements, not evidence that the original Windows 0.5–1 second lead is resolved.
- Native timeline geometry, zoom/scroll and real waveform tests passed; the captured guide preview was visually inspected.
- Independent review found a supplementary-Han lexical classification defect; a failing regression reproduced the lost leading interval, and codepoint-based classification fixed it.
- Output: `build/subtitle-word-timing-acceptance/`. No Windows execution or listening-accuracy claim. Existing captions and earlier comparison files were preserved. Changes remain local and uncommitted.
