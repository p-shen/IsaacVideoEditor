# Titles and text labels

Move the timeline playhead to the desired moment and choose **Add text**. The new overlay is selected and the **Text** inspector opens. Type an opening title, a place name or a short description. Add more overlays in the same way. The trash button beside the overlay selector in the **Text** inspector deletes the selected overlay; Undo restores it.

The inspector offers multiline text, font, numeric size, color, bold, shadow and optional fade in/out. Click the color swatch to choose a color and use the **B** toggle for bold; hover or keyboard focus reveals each control’s name. **Clean**, **Playful** and **Background box** provide starting styles with visual samples in the chooser. Shadow and fade options live in **Effects**, and safe margins in **Guides**. The empty Text inspector also offers **Add text**. Drag text in the viewer to position it, or open **Placement** for a position preset and exact percentages. With the viewer focused, arrow keys move selected text one display pixel; Shift moves ten.

Drag a text block along its timeline row to move it, or drag either edge to change its duration. The block shows its start and end time. Overlapping overlays use additional rows, and later overlays draw on top. **Length** inside **Placement** provides a numeric duration alternative in seconds. Integrated steppers and Up/Down adjust one frame. In the timeline, Option/Alt with Left/Right moves selected text one frame; adding Shift changes its duration. Escape cancels a drag, and a completed drag or text-entry session is one Undo step.

Use the time ruler/playhead to preview the edit. Ordinary timeline arrow keys seek one frame; Page Up/Down seek a second. There is no separate playback-position slider.

Text defaults to absolute Project time when video is trimmed or reordered. Open **Attachment** for explicit Attach to clip and Detach controls; see [Captions and SRT](captions.md) for their shared source-frame rules. Rendering stops at the video end while saved overlays remain available when an edit is extended or undone. Size and position scale with the video canvas. The same compositor draws preview frames and exported MP4 frames, including shadow, background and fades. Selection borders are editing controls and are not exported.

New saves use version 7. Versions 1–6 remain loadable. Text content, style, position, order and timing remain part of persistence and Undo/Redo. Captions have their own data, panel and output controls; importing captions never changes these manual titles.

## Verification

The caption extension reuses this title implementation and its shared renderer. The complete integrated suite now has 23 targets, covering the original title tests plus caption commands, SRT, attachments, recovery and output. The [caption guide](captions.md) and [implementation plan](superpowers/plans/2026-10-26-captions.md) record the current schema and verification. The original opening-title and place-label workflow remains available.
