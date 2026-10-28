# Media, audio and composition

This release follows the [editing interaction design](superpowers/specs/2026-10-25-qt-quick-interaction-design.md), priorities 4–6 and 9–10; the rank-7 visual-overlay interface is paused at the user’s request. The independent [text-overlay feature](text-overlays.md) is preserved. The [titles/captions cross-check](remaining-interaction-roadmap.md) records the completed rank-8 work and its [caption guide](captions.md).

## Choose moments before adding them

**Import** adds reusable video/audio sources to **Media**. It leaves the movie unchanged. **Import and append…** keeps the direct import-to-movie workflow. Media has filename search, type and usage filters, and usage counts. Removing an entry from Media preserves its existing instances and the original file; used entries require confirmation.

Select a source to enter **Source** viewer mode. Its cursor and marked range are separate from the Project playhead. **I** marks the first frame; **O** includes the current frame by placing the exclusive end one frame later. Numeric In/Out fields and **Clear Range** provide alternatives. **Append** adds the range at the end. **Insert** adds it at the committed Project playhead, splitting footage there when allowed. An insertion inside a crossfade is rejected with guidance.

Drag from Media onto the main timeline to insert its marked range. A drop into Media imports only. The timeline shows the proposed insertion and duration before release. One source can supply many independently trimmed instances. Source ranges, cursor positions and Media contents survive saving.

## Mix audio directly

Drag a narration block to change its absolute start. Drag a gain line vertically to change dB, or the square end handles horizontally to set fades. The **Audio** inspector shows one source at a time, with volume, mute and music looping visible. **Timing and fades** contains fades and narration start in seconds; integrated steppers and Up/Down adjust one frame. **Preview and levels** contains solo and metering. File replacement, recent downloads and Mixkit live in **File…**. Moving narration changes placement; it does not trim its source.

**Loop to end** explicitly controls music repetition. With looping disabled, music stops at its source end. Music fades apply at the outside of the playable block, rather than at each repetition. Waveform display scaling remains independent of gain.

**Solo** is labeled **SOLO PREVIEW** in the Project viewer. **Full mix** restores normal preview. Solo never changes the exported mix. Saved gain, mute and fades use the same rendering path for preview and export. The master meter reports playback dBFS; a clipping indication stays lit until **Reset**.

Release commits one audio gesture. Escape or lost capture cancels it. Trimming clamps fades to the playable duration. Recording, ducking and noise reduction remain outside this manual mixing release.

## Recover work and locate moved files

The production app writes a separate recovery snapshot after two seconds without a committed edit, and at least every 30 seconds during continuous editing. Untitled projects are included. Open **File → Recovery and offline media…** to view recovery status and saved snapshots. **Retry recovery** retries a failed write; failure never marks the project saved.

After an interrupted session, **Recover** opens the snapshot as unsaved work. **Discard** removes that snapshot. Neither replaces the manually saved project. Active sessions keep separate locked journals. Pointer, text, source-scrub and canvas previews do not enter recovery before commitment.

Offline sources keep their timing and edits. Narration and music retain their last known duration independently of Media entries, so removing an entry from the library does not remove its offline audio block. Open **File → Recovery and offline media…** and choose **Locate file…**, or **Search a folder…** to review matching filenames. Duplicate filename matches require a choice. Replacements must contain the required media type and cover saved audio lengths, trims and marked source ranges. Relinking changes every reference to that path in one undoable edit. Conflicting Media ranges are retained and explained rather than silently merged. Image overlays can be relinked to image files. Older files capture their audio duration when opened with the source available; unknown durations cannot be reconstructed from a missing original.

## Export while continuing to edit

**File → Export…** captures the current revision and opens a nonmodal panel showing destination, dimensions, 60 fps, H.264/AAC, original-media use and revision. **Keep editing** hides the panel while the job continues. Changes made afterward do not affect that export.

Export runs in a separate process. If the renderer crashes, the editor stays open and preserves the existing output. A hardware encoder crash is retried once with software. The panel shows the active encoder; **Open export log** opens diagnostics with progress, encoder selection, fallback reasons, and native errors.

**Cancel export** stops the worker and removes its temporary output. Completed output replaces the destination atomically; canceling preserves an existing destination. A failed or canceled job offers **Retry same revision**. A completed job offers **Reveal in folder** and **Open video**. Original clip, audio, overlay and library files are protected from destination collisions. Text layouts are reused across frames in a bounded cache, preserving fonts, positions, fades, and live edits.

## Visual overlays paused

The extra V1–V3 video/image tracks and their add, layer and editing controls have been removed from the interface for now. Existing projects retain their saved visual-overlay data and rendering. Main-sequence editing, text/caption tracks, clip attachments for text, and canvas/transform tools remain available.

This removal passed all 23 automated test suites. Native Mac checks covered the simplified light/dark layouts, narrow windows with large text, caption editing and main-clip transform cancellation. The rendering/model tests remain in place for saved-project compatibility.

## Reframe and animate

**Transform** opens the Transform inspector. Framing, scale, rotation and preview manipulation are primary controls; **Position and opacity** and **Animation** reveal specialist controls. **Project canvas** separates project-wide format from selected-clip edits. Canvas presets include legacy **Auto resolution**, landscape, portrait, square and custom even dimensions, shown only for **Custom**. Changing a canvas starts a proposed preview with **Apply** and **Cancel**. Starting another editing action cancels an unapplied visual preview before creating the new command. Each instance retains its explicit **Fit**, **Fill** or **Crop** choice.

In transform mode, drag the viewer picture to reposition it, corners to scale uniformly, or the rotation handle to turn it. Center guides and exact values accompany the gesture. Numeric position, scale, rotation, opacity and crop edges provide alternatives. Escape cancels, and Reset clears the transform and its animation. Existing quarter-turn rotations remain compatible.

The first keyframe diamond enables animation for that property. Editing it at another playhead frame adds or updates a key there. Previous/next, delete, move and Linear/Ease controls operate on source-frame timestamps. Splits copy the curves to both children. Trims retain hidden keys and evaluate the curve at the new visible source times. Preview proxies retain their source-frame origin, so prepared copies and original-resolution exports evaluate the same motion.

## Set a transition at one cut

Select a cut marker or choose a cut in **Transitions**. Set **Cut** or **Dissolve** and enter seconds. **Precision and presets** retains frame values and the draggable dissolve preset; select a cut before clicking or dragging the preset. Steppers and Up/Down adjust one frame. The timeline previews effective overlap and total duration. Choosing **Cut** removes the dissolve.

Neighboring cuts retain their settings. Available media bounds local overlap and reserves a playable frame in short clips; the inspector shows the requested and actual duration when a fade is shortened. **All existing cuts** stages a crossfade duration without changing the edit. **Apply to all existing cuts** replaces transitions at every current boundary, including appended clips, split points, and cuts previously set to **Cut**. Short clips shorten only nearby crossfades; distant fades keep the requested duration. The result notice reports the duration, number of cuts, and how many fades were shortened, with **Undo** to restore the previous transitions. Set the duration to zero and apply it to remove crossfades. Cuts added or split afterward keep their own settings; apply again to include them. Older projects retain their timing until you explicitly apply the new behavior.

## Files and verification

New saves use project version 7, extending version 6 with separate captions and text attachments while retaining reusable assets, audio controls, per-cut settings, stable clip identities, visual layers, canvas and animation. Versions 1–6 remain loadable with their original timing, rotation and text. Original media is never modified.

The advanced-editing increment passed all 21 CTest targets on macOS 15.7.3 (Apple silicon), Qt 6.12 and installed MLT 7/FFmpeg. Tests cover real PCM/AAC and MP4 pixels, source ranges, connected attachments, source-time curves/proxy origins, local overlaps, gesture undo/cancel, forced-process recovery, repaired write failures, ambiguous relinks, frozen export revisions and mid-render cancellation preserving existing output. Independent review reproduced two important problems: interleaved canvas/audio edits could corrupt undo history, and some numeric fields could commit after Escape. Both were corrected with failing-then-passing regression tests. A further real-pointer test corrected source-scrub movement after cancellation. The delivered project was rebuilt from clean sources, then rebuilt after the final audio-duration correction; its final full run passed 21/21 in 39.93 seconds with normal user-cache access. The offline-audio regression also covers saved durations without library entries, legacy loading, shorter replacement rejection and undo; focused independent review found no further important issue. Native Cocoa checks passed all eight entries, including empty-viewer import/window-title save status, light/dark layouts, 760×560 at 24 pt, direct overlay gestures, numeric Escape and focus visibility. Physical-output listening, VoiceOver, real-camera performance profiling and other operating systems remain release checks. The latest independent UI refinements are preserved: empty-viewer import, compact icons and window-title save status.

The subsequent [caption extension](captions.md) adds two test targets, with the complete current suite passing 23/23 before delivery.
