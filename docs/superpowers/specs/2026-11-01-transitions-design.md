# Transition presets

Implement the first five families recommended in chat and approved by “Implement”: four directional wipes, two diagonal wipes, circle open/close, horizontal/vertical barn doors, and horizontal/vertical blinds. Keep Cut and Dissolve.

Use the existing MLT luma renderer and audio mix. A small shared preset catalog supplies stable IDs and display names to validation, rendering, the inspector, and timeline gestures. Generate reusable grayscale masks locally; no new dependencies, shader pipeline, or packaged plugins. Preview and export use the same transition configuration.

Selecting another effect at the same duration must commit an undoable change. Duration fields and timeline overlap dragging retain the selected effect. Save/load retains effects, rejects unknown IDs, and loads existing Cut/Dissolve projects unchanged. Keep the existing half-second draggable Dissolve preset and bulk crossfade operation.

## Approaches

The accepted approach expands MLT mask transitions, which fits the current renderer. Direct GL shader integration requires a separate rendering path. Moving-image transitions such as push and zoom require additional compositing and remain outside this increment.

## Verification

Test all presets through project persistence, real rendered frames, and a real MP4 export. Exercise type-only changes, undo/redo, short clips, duration edits, timeline resizing and cancellation. Check the inspector's keyboard selection and layout. Run the complete existing CTest suite after building.
