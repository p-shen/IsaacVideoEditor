# Interaction roadmap and caption cross-check

October 26, 2026. Implements the approved [interaction design](superpowers/specs/2026-10-25-qt-quick-interaction-design.md), following the Qt Quick migration.

## Cross-check: “Plan text overlay features”

That chat has delivered manual titles and descriptive labels, a timed text lane, overlapping text, viewer positioning, multiline typography and presets, fades, undo/cancel, persistence and one compositor for preview/export. The delivered implementation and verification are in [text-overlays.md](text-overlays.md). It also intentionally removed the redundant playback-position slider.

The caption extension completes the remaining rank-8 requirements using the existing manual-title system. See [Captions and SRT](captions.md).

| Requirement | Current state |
| --- | --- |
| Manual titles, typography, frame timing, stacking, matching export | Delivered |
| SRT caption import and separate SRT export | Delivered |
| Caption list, previous/next, explicit preview at caption start | Delivered |
| Caption-row Enter/Shift+Enter/Escape editing | Delivered |
| Caption multi-selection, batch styles, Apply style to all | Delivered |
| Safe margins and overlap warnings | Delivered; overlaps are retained and warned |
| Project-time indicator and Timing needs review after sequence changes | Delivered; Project time remains the default |
| Attach/detach title/caption to a host source frame | Delivered using the shared stable host/source-frame rules |
| Optional burn captions into video independent of SRT output | Delivered independently; titles continue rendering |
| Multiline and resolution-scaled rendering | Delivered with caption-specific non-Latin and scaled-frame coverage |
| Automatic transcription | Explicitly deferred by both designs |

Current text data must remain editable and old projects must retain identical text timing and rendering. No automatic conversion of titles into captions.

## Authorized delivery sequence

4. Direct audio editing: narration placement, saved gain/mute/fades for source audio, narration and music, preview solo and output metering.
5. A reusable media library, Source viewer/ranges and explicit Append/Insert.
6. Recovery snapshots, offline relinking, and cancellable export of a frozen revision while editing continues.
7. Connected visual overlays: interface temporarily removed at the user’s request. Saved overlay content remains compatible; text/caption attachment stays available.
9. Explicit canvas, static viewer transforms and source-time keyframe curves.
10. Individual-cut transitions alongside the existing global convenience.

Each increment preserves current titles, styling, legacy projects, original media, 60 fps exclusive ends, native output, undo/cancel and preview/export agreement. Rank 8 is delivered by the text/caption work; the caption plan records its checks. Automatic transcription remains deferred.
