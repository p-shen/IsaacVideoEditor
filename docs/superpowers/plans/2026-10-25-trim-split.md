# Per-clip length and split

The user confirmed that splitting means editable timeline pieces. Keep original files intact, preserve the 60 fps project timeline, and make all edits undoable.

- [x] Add Length beside Start/End, anchored to the source start, limited to source duration and rounded to whole frames.
- [x] Add Split in the timeline toolbar and Edit menu (Command+B / Ctrl+B). Select the right piece after splitting; allow repeated splits. Disable at ends, outside selection, and inside crossfades.
- [x] Represent new split boundaries as hard cuts so splitting does not add a fade, shorten the edit, or alter other clip positions. Persist in version 3; continue loading versions 1 and 2.
- [x] Test length bounds, frame continuity, repeated split, fades, save/load, actual MLT timeline/export, and Undo/Redo. Build and visually check the updated Mac app.

Verification: Release build succeeded; all five CTest targets passed. Independent review found no confirmed defects. MLT decoded all 300 frames identically before/after a split with existing crossfades. A split-and-trim MP4 export matched its 285-frame / 4.75-second model at 1920×1080 and 60 fps, with AAC audio and clean FFmpeg decoding. Dark/light and large-text screenshots were checked; live VoiceOver remains unchecked.
