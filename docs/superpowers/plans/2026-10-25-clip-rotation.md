# Per-clip rotation

Add left 90°, right 90°, and 180° turn buttons in the Clip inspector. Interpret “flip 180 degrees” as turning the image upside down. Keep the source file intact and retain the same rotation in preview and export, with the entire image fitted to the 1080p canvas.

- [x] Store canonical clockwise rotation (0/90/180/270) on each Clip; compose repeated turns, retain through trims/splits/reorder, and persist in version 4 while loading versions 1–3.
- [x] Apply the rotation in the shared MLT timeline path, in addition to existing source orientation metadata. Verify asymmetric images for direction and fit.
- [x] Add labelled native buttons and current orientation, disabled without selection, using existing Undo/Redo and playhead preservation.
- [x] Verify model/persistence, per-clip UI behavior, real decoded frames and an MP4 export; rebuild IsaacVideoEditor.app and check both themes.

Verification: Release app build succeeded, and all six CTest targets passed. Permanent pixel tests check clockwise orientation, fit, repeated-source isolation, and rotation added to camera orientation. Independent review found no confirmed defects. A decoded MP4 export verified all four orientations at 1920×1080, 60 fps, 240 frames / 4 seconds with AAC audio and clean FFmpeg decoding. Dark/light and large-text UI renders were checked. Live VoiceOver remains unchecked.
