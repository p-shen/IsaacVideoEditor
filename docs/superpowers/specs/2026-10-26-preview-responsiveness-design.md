# Preview responsiveness

Implement the three performance improvements approved in chat: overlay preview copies, responsive visual drags, and cached timeline artwork. Keep the existing C++17/Qt Quick/MLT engine and preview worker.

## Approach
Reuse range-based PreviewCache for visible video overlays, including source-frame offsets for animation. Update existing visual filters on the still-decoding worker during a gesture; coalesce pending edits with seeks. Rebuild once at commit/cancel and when canvas or media topology changes. Cache static timeline artwork at display pixel density; draw playhead, skim and focus separately.

A replacement video engine or wholesale scene-graph rewrite would increase scope and regression risk. Debouncing full rebuilds alone would leave interface stalls. The chosen approach reuses the current engine and worker.

## Constraints
Preserve original media, saved source timestamps, frame attachments, animation timing, camera orientation, undo/redo, gesture cancel, native export resolution, and keyboard access. Preview-only offsets must never be saved. Do not restart the user's open editor with unsaved work. No new dependencies. The workspace has no Git repository; keep a source baseline in /private/tmp instead of creating a worktree.

## Verification
Real media tests must cover overlay preparation and fallback, proxy animation origins, asynchronous/coalesced visual updates, cancel and undo, crossfades, timeline cache reuse/invalidation, and pixel parity. Build Release, run the full CTest suite, and compare repeated timeline-paint time on the same fixture. Hardware performance varies with background system load; report measured results without extrapolating them to all projects.
