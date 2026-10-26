# Timeline workspace implementation plan

**Goal:** Give the local editor a familiar browser, viewer, inspector, and timeline workflow inspired by CapCut and Final Cut.

**Architecture:** Keep C++17, Qt Widgets, MLT, and the existing project format. A new timeline widget emits editing requests; MainWindow validates and applies edits. Native clip and numeric controls remain available alongside direct manipulation.

**User direction:** “go ahead” — choose a sensible first version and implement it without further design questions.

## Constraints

- One continuous video sequence, one narration source, looping music, and a shared crossfade length.
- 1920×1080 at 60 fps; existing projects retain timing.
- No additional dependencies. System fonts, light/dark palettes, visible focus, and keyboard alternatives.
- Preserve the playhead across edits; selection reveals a clip's position.

## Tasks

- [x] Add a proportional timeline with a ruler, playhead, video/audio lanes, seeking, reorder, and edge trimming. Test overlap geometry, seek, reorder, and trim bounds.
- [x] Reorganize MainWindow into resizable browser/viewer/inspector and timeline panels. Move Export to the header; keep project and save state visible. At narrow widths put inspector beside Clips as a tab.
- [x] Connect selection, edit requests, zoom/fit, numeric trim and audio controls. Preserve preview position and provide Undo/Redo through Qt's undo stack. Test selection seeking, position preservation, and undo/redo.
- [x] Refine shared themes for a quieter professional workspace. Verify both themes and large text visually.
- [x] Build the Mac app, run the complete test suite, review implementation independently, and update screenshots and usage notes.

## Review focus

Crossfade overlaps must match the MLT timeline. Trims stay inside source media and retain at least one frame. Short edits and empty lanes remain usable. Undo restores actual model state and preview position. Resizing and larger text must not hide essential controls.

## Verification

Release Mac build succeeded. All five CTest targets passed after the final changes. Dark/light workspace screenshots are in docs/editor-preview.png and docs/editor-preview-light.png. Normal and 24 pt text were visually checked at desktop and narrow sizes; short windows scroll instead of compressing controls. Independent review findings about Undo selection and timeline click mapping were fixed and covered by regressions. Live VoiceOver and Windows/Linux builds remain unchecked.
