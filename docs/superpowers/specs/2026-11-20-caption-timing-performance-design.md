# Caption timing performance

The user approved the first optimization from the performance investigation: resolve caption timing once and avoid repeated lane calculations during timeline interaction. Synthetic Release measurements with 3,000 attached captions found one start-time sort took 216 ms with 100 clips and 2,221 ms with 1,000 clips.

Use a caller-owned `Project::TextTiming` value containing resolved title/caption starts, project duration, and caption-inclusive extent. Build it with one clip timing pass and one host-ID lookup. Public project fields remain mutable, so Project must not retain a hidden cache. Rendering and caption-list construction consume one snapshot per operation; QuickTimeline retains snapshots at project/gesture boundaries.

Timeline lane assignment compares resolved starts and durations with its previous inputs and sorts only when those inputs change. Zoom, pan, font, selection, and style changes reuse lanes. Gesture previews refresh their timing after each mutation and cancellation restores the committed timing. Painting and snapping use the same resolved starts.

Approaches considered: a hidden mutable Project cache risks stale values after direct field edits; moving the existing repeated scans to workers retains unnecessary work; caller-owned snapshots remove those scans and follow the existing TimelineData pattern. The approved first patch uses snapshots and targeted lane invalidation. Recovery, decoder handoff, persistent caption models, media preparation, and GPU changes remain separate work.

Preserve 60 fps timing, source offsets, transitions, attachment clamping, desired caption duration beyond the project end, rendering order, undo, cancellation, and existing persistence. No dependencies or project-format changes. Work in the existing feature checkout to preserve the user's uncommitted caption work; do not commit it implicitly.

Success: timing/interaction regressions pass, long-project sorting and preview-data construction no longer repeat whole-project timing scans, the full CTest suite passes, and the running app is checked with synthetic media.
