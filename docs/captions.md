# Captions and SRT

Open **Captions** beside the timeline, or choose it in the inspector. Captions are separate from manual titles: importing captions never converts or replaces an opening title or place label.

## Create and correct

**Import SRT** appends UTF-8 SubRip captions. The list shows each caption’s frame interval and whether it uses Project time or an attachment. Overlaps and captions past the movie end are retained and flagged. Import rejects malformed files before changing the project. **Add caption** creates a row at the playhead; the trash control deletes selected captions. Source media is never changed.

Click a row or timeline interval to select it. Selection keeps the playhead in place. **Previous/Next** move through captions in time order; **Preview caption start** deliberately shows its first frame. Double-click a row, or press **Enter**, to edit. **Enter** commits, **Shift+Enter** adds a line, and **Escape** restores the previous text. Space and editing shortcuts stay inside the text editor. A committed edit is one Undo step, including after saving while the field remains focused.

Use Command/Ctrl-click or Shift-click to select several rows or intervals. The shared font, numeric size, color, bold, shadow, style, position and fade controls affect the selected captions. **Apply style to all captions** explicitly copies the current appearance across the entire set in one undoable command.

## Set timing and attachments

Drag either timeline edge to adjust timing; captions have a one-frame minimum at 60 fps. Drag a block to move it. Option/Alt with Left/Right moves a selected caption one frame; adding Shift changes its duration. **Duration (frames)** provides a numeric alternative. Escape or lost capture cancels a gesture. Overlapping captions occupy additional rows, with a warning rather than dropped text.

Captions default to **Project time**. Reordering or changing the sequence duration keeps their absolute times and marks them **Timing needs review**. Correct intervals, then choose **Mark timing reviewed**. Review and warning state is saved and undoable.

Both captions and titles offer **Attach to clip** and **Detach**. Choose the numbered host instance. Attach connects the start to a source frame; reorder follows the host, split transfers the connection to the appropriate child, and trim clamps a removed connection frame to the retained source. Detach retains the current absolute position. Host deletion offers removal of its attached visuals/text or detachment in the same Undo step. Requested text duration is retained when the project is temporarily shorter; visible video is clamped at its end. SRT preserves requested caption intervals.

Safe-margin guides are editing controls inside the video canvas. They never appear in output. Fonts use the installed family and Qt’s supported glyph fallback; text layout uses the same reference coordinates in preview and original-resolution export.

## Output and project files

**Export SRT** writes captions separately in chronological order, with millisecond conversion that round-trips 60 fps frame boundaries. Manual titles and project styling are not part of SRT. Common input bold/italic/underline/font tags are normalized to plain editable words; entities and line breaks are decoded. Literal angle brackets and ampersands are escaped on SRT output so they round-trip as text. **Burn captions into video** independently controls captions in preview and MP4; titles continue to render. An in-progress video export retains its captured caption revision while you continue editing. Recovery includes committed captions and excludes active drafts.

New saves use version 7. Versions 1–6 remain loadable, retaining their title timing and style. SRT import supports up to 10000 cues, 2000 UTF-16 units per cue, intervals within 24 hours and a 128 MB file. Automatic speech transcription remains deferred. A positive sub-frame cue ending exactly at the 24-hour limit can be rejected by frame rounding; shift its start to the last full frame before importing.

## Verification

The delivered project at `/Users/Peter/Documents/ChatGPT/IsaacVideoEditor` clean-builds in Release mode and passes all 23 test targets (89.84 seconds). Tests cover SRT UTF-8/BOM/multiline/empty text, frame rounding, overlaps, legacy persistence, malformed input, public commands, batch/all styles, Undo/cancel, recovery drafts, frozen exports, source-frame attachments and caption pixels in MP4. Additional native checks pass for keyboard/double-click editing, safe guides, light/dark themes and narrow/large-text layouts, with Japanese/Arabic captions and independent SRT/video output. Screenshots and exported frames were visually checked. Delivery checks are recorded in the [implementation plan](superpowers/plans/2026-10-26-captions.md).

Verification covers this Mac. Other operating systems, VoiceOver, physical listening and camera-performance checks remain release work. The Mac was locked during an optional final check of the user's open production window; the completed native test interactions and export checks ran successfully before that check.

The [Library of Congress SRT description](https://loc.gov/preservation/digital/formats/fdd/fdd000569.shtml) documents its basic HTML-derived formatting. This editor recovers plain words from that subset and keeps appearance editable in the project. SRT cue separators also make blank paragraphs ambiguous; outer whitespace is normalized and paragraphs resembling cue headers require cleanup.
