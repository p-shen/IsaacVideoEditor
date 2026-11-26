# Captions and SRT

Open **Captions** beside the timeline, or choose it in the inspector. Captions are separate from manual titles: importing captions never converts or replaces an opening title or place label.

## Create and correct

### Generate locally from the edited timeline

Open **Captions → Generate subtitles**. Choose **Large-v3** for the full Whisper model or **Turbo** for faster drafts. The choice is remembered on this computer. Download each model once (about 3.1 GB and 1.6 GB respectively); its checksum is verified before use. Transcription then runs offline. Audio and transcripts are not uploaded, and no provider account is needed.

Choose **Generate**, select an output path and wait for audio preparation/transcription to finish. The compact panel shows the current stage and progress; **Cancel** preserves existing output. Generation uses a captured revision of the timeline's complete audible mix, including trims, cuts, transitions, narration, music and audio controls. It uses original sources, saves the SRT separately and does not alter captions or dirty the project. Subtitle generation and video export run one at a time.

New generations request Whisper's experimental word/token timing. A caption uses its first and last spoken-token bounds when the complete text has valid, ordered timings inside the segment. Incomplete word timing keeps the original segment interval and lists the affected caption numbers for review. This refines individual boundaries without applying a blanket delay or changing the words. It is not guaranteed forced alignment or a speech/noise classifier; listening review is still needed.

Review the words and timings, then choose **Import**. **Reveal** opens the saved SRT's folder. Existing captions are appended only after confirmation; Undo removes the imported set in one step. If the project changed during generation, one-click import is disabled and the saved SRT remains available for manual review. A change to a required source invalidates generation. Brief intervals and overlaps are flagged. Whisper timestamps are estimates, not verified word alignment; listening review is still needed. Long runs of the same substantial phrase trigger one automatic retry with previous-text history disabled between Whisper decoding windows. The panel shows **Retrying transcription**; a recovered result carries a review warning. If the retry still loops or fails, the existing SRT is preserved and the job reports failure. The check is conservative (at least eight nearby repetitions spanning fifteen seconds), so unusually repetitive real speech may also require review. It does not remove repeated captions or skip audio. Instantaneous decoder fragments with zero duration are omitted with a review warning; reversed or out-of-range timestamps still fail validation.

The module transcribes English/Mandarin without translating and normalizes Chinese to Simplified on Windows/macOS. Open the gear button for **CPU**, engine/device details and **Logs**. CPU explicitly selects processor-based inference; otherwise the actual GPU backend must initialize successfully. Windows packages include an isolated CUDA worker and CPU alternative; macOS uses Metal or CPU. Model/device choices are local application state, not part of saved projects. This module does not inherit the separate Qwen skill's five-minute alignment limit. The Qwen skill remains available independently.

Builds without a bundled engine retain caption import/editing and explain that generation is unavailable. See [local engine setup](building.md#local-subtitle-engine) for development builds. Windows CUDA and non-ASCII Windows paths need acceptance on Windows hardware; Mac checks cannot establish them.

**Import captions** appends UTF-8 SubRip captions. The list shows each caption’s interval in seconds and whether it uses Project time or an attachment. Overlaps and captions past the movie end are retained and flagged. Import rejects malformed files before changing the project. **Add caption** creates a row at the playhead; the trash control deletes selected captions. Source media is never changed.

Click a row or timeline interval to select it. Selection keeps the playhead in place. **Previous/Next** move through captions in time order; **Preview caption start** deliberately shows its first frame. Double-click a row, or press **Enter**, to edit. **Enter** commits, **Shift+Enter** adds a line, and **Escape** restores the previous text. Space and editing shortcuts stay inside the text editor. A committed edit is one Undo step, including after saving while the field remains focused.

Use Command/Ctrl-click or Shift-click to select several rows or intervals. Open **Appearance** for shared font, numeric size, color, bold, shadow, style, placement and fade controls. They affect the selected captions only when edited. **Apply style to all captions** explicitly copies the current appearance across the entire set in one undoable command.

While editing, select words or Chinese characters and use **Selected words** to choose their text color or toggle bold. Command/Ctrl+B toggles bold for the selection. Tab reaches the controls without losing the selection; the video preview shows the formatting. Enter commits content and formatting together, and Escape restores both. Corrections retain formatting on surviving text and replacements inside a styled range. Joined emoji are kept intact. Whole-caption Appearance settings remain the base style; selected-word overrides take precedence. Animated karaoke and Bold Highlight emphasis can temporarily override a word's color.

## Set timing and attachments

Drag either timeline edge to adjust timing; captions have a one-frame minimum at 60 fps. Drag a block to move it. Option/Alt with Left/Right moves a selected caption one frame; adding Shift changes its duration. **Start** and **Length** provide numeric timing in seconds; integrated steppers and Up/Down adjust exactly one frame. Escape or lost capture cancels a gesture. Overlapping captions occupy additional rows, with a warning rather than dropped text.

Selecting a caption shows dashed start/end guides through the existing video, narration and music waveform lanes. Zoom in to compare its edges with the audio and use the normal trim controls for corrections. Waveforms show source peaks, not speech classifications or a measurement of the final mixed signal; background noise can also make a peak.

Captions default to **Project time**. Reordering or changing the sequence duration keeps their absolute times and marks them **Timing needs review**. Correct intervals, then choose **Mark timing reviewed**. Review and warning state is saved and undoable.

Both captions and titles offer **Attach to clip** and **Detach** inside **Attachment**. Choose the numbered host instance. Attach connects the start to a source frame; reorder follows the host, split transfers the connection to the appropriate child, and trim clamps a removed connection frame to the retained source. Detach retains the current absolute position. Host deletion offers removal of its attached visuals/text or detachment in the same Undo step. Requested text duration is retained when the project is temporarily shorter; visible video is clamped at its end. SRT preserves requested caption intervals.

Safe-margin guides are editing controls inside the video canvas. They never appear in output. Fonts use the installed family and Qt’s supported glyph fallback; text layout uses the same reference coordinates in preview and original-resolution export.

## Output and project files

**Files and all captions → Export SRT** writes captions separately in chronological order, with millisecond conversion that round-trips 60 fps frame boundaries. Manual titles and project styling are not part of SRT. Common input bold/italic/underline/font tags are normalized to plain editable words; entities and line breaks are decoded. Literal angle brackets and ampersands are escaped on SRT output so they round-trip as text. **Burn captions into video** independently controls captions in preview and MP4; titles continue to render. An in-progress video export retains its captured caption revision while you continue editing. Recovery includes committed captions and excludes active drafts.

New saves without selected-word formatting use version 8; saves with selected-word formatting use version 9. Versions 1–8 remain loadable, retaining their timing and style. Older app versions cannot open version 9 projects. The text presets and independent animations described in [Titles and text labels](text-overlays.md) remain available. SRT import supports up to 10000 cues, 2000 UTF-16 units per cue, intervals within 24 hours and a 128 MB file. A positive sub-frame cue ending exactly at the 24-hour limit can be rejected by frame rounding; shift its start to the last full frame before importing.

## Verification

Experimental word-bound refinement passed the 39-target macOS suite plus final Unicode/word-timing regressions. Both cached Whisper models completed real local generation and import/Undo. Selected-caption waveform guides were checked in a native window and across zoom/scroll. These checks do not verify timing accuracy on the original Windows recording.

The compact generation panel passed native macOS keyboard checks for options, Logs during generation, cancellation, result import/Undo and saved-result navigation. Its narrow layout was checked at 300 px with 20 px text; the final 39-target suite passed. Other platforms were not rechecked for this presentation change.

Local subtitle generation passed the 39-target CTest suite on macOS. Both full large-v3 and Turbo models generated SRT from the captured timeline through the bundled Apple GPU worker; optional import and Undo passed. Audio checks cover narration placement and fades, music, transition timing and timestamps beyond five minutes. The signed Mac package passed clean-runtime smoke checks, including a missing transcription worker. Windows CUDA/Unicode paths and Linux execution still need checks on those platforms; transcript accuracy requires listening review.

Selected-word formatting passed the 34-target CTest suite on macOS, including project/SRT round-trips, malformed ranges, Unicode corrections, Undo/cancel/recovery and styled MP4 output. Native Mac window checks cover keyboard selection, Tab/Backtab, bold, color-dialog acceptance/rejection and cancellation after a focused Save. Automated dialog checks use Qt dialogs; the macOS system color panel and other platforms remain unverified.

The delivered project at `/Users/Peter/Documents/ChatGPT/IsaacVideoEditor` clean-builds in Release mode and passes all 23 test targets (89.84 seconds). Tests cover SRT UTF-8/BOM/multiline/empty text, frame rounding, overlaps, legacy persistence, malformed input, public commands, batch/all styles, Undo/cancel, recovery drafts, frozen exports, source-frame attachments and caption pixels in MP4. Additional native checks pass for keyboard/double-click editing, safe guides, light/dark themes and narrow/large-text layouts, with Japanese/Arabic captions and independent SRT/video output. Screenshots and exported frames were visually checked. Delivery checks are recorded in the [implementation plan](superpowers/plans/2026-10-26-captions.md).

Verification covers this Mac. Other operating systems, VoiceOver, physical listening and camera-performance checks remain release work. The Mac was locked during an optional final check of the user's open production window; the completed native test interactions and export checks ran successfully before that check.

The [Library of Congress SRT description](https://loc.gov/preservation/digital/formats/fdd/fdd000569.shtml) documents its basic HTML-derived formatting. This editor recovers plain words from that subset and keeps appearance editable in the project. SRT cue separators also make blank paragraphs ambiguous; outer whitespace is normalized and paragraphs resembling cue headers require cleanup.
