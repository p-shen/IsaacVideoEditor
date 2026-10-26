# Qt Quick adoption and editing interaction design

Date: October 25, 2026  
Product: IsaacVideoEditor  
Decision: Adopt Qt 6 Quick, QML, and Qt Quick Controls for the desktop interface. Retain C++17, MLT, and the existing FFmpeg integration for editing and media processing.  
Status: Qt Quick/QML migration and priorities 1–3 are delivered. Priorities 4–7 and 9–10 are implemented in the integrated build; see [advanced editing](../../advanced-editing.md). Manual titles were independently delivered; the [rank-8 cross-check](../../remaining-interaction-roadmap.md) records remaining captions work. See [migration verification](../../qt-quick-migration.md).

The immediate goal is to make assembling and refining a short vlog feel direct and predictable. Prioritize seeing the media, finding the right frame, and making a precise cut before adding effects or decorative animation. CapCut and Final Cut Pro provide interaction references; these designs define IsaacVideoEditor's behavior rather than promising identical feature sets or internal architecture.

## Highest impact first

The order reflects frequency of use, reduction in editing friction, and fit with the current short-vlog workflow. It is a product priority order, not a claim that every feature has the same implementation cost. Accessibility, correct saves, undo, and preservation of source files apply to every stage.

| Rank | Interaction gap | User benefit | Existing support | Additional scope |
| --- | --- | --- | --- | --- |
| 1 | Filmstrips and audio waveforms | Recognize shots, speech, silence, and music without opening each clip | Timeline blocks show filenames and durations | Derived-media cache and visible-region rendering; no content schema change |
| 2 | Responsive scrubbing, skimming, and anchored zoom | Find a moment without losing position or waiting for obsolete frames | Ruler seeking, frame keys, zoom buttons, Fit, and proxy preparation | Asynchronous preview coordination and new navigation gestures |
| 3 | Precision trimming, snapping, and batch selection | Make accurate cuts and rearrange sequences confidently | Single-clip trim, split, reorder, remove, and undo | Gesture transactions and selection state; advanced trim modes later |
| 4 | Audio edited directly in the timeline | Balance speech and music and place fades by ear and waveform | One narration source, narration offset, looping music, music gain | Audio control model and preview/export changes |
| 5 | Source browser with ranges and explicit insert or append | Choose usable moments before adding them to the movie | Import immediately adds complete sources to the edit | Separate assets from timeline instances; project format extension |
| 6 | Recovery, relinking, and nonblocking export | Protect work and keep the application useful during long tasks | Atomic manual save, dirty-state prompts, export progress | Recovery storage, media relinking, cancellable export jobs |
| 7 | Connected B-roll and overlay clips | Cover dialogue edits and add visual context | One continuous video sequence | Layered project model, compositing, and attachment rules |
| 8 | Titles and editable captions | Make a vlog readable and ready to share | No title or caption model | Timed text, typography rendering, import/export; transcription optional later |
| 9 | Viewer transforms, canvas formats, and keyframes | Reframe footage and animate simple moves | Per-clip quarter-turn rotation with image fitting | Transform persistence, compositing, project canvas settings, animation |
| 10 | Transitions controlled at individual cuts | Apply a deliberate transition without changing every cut | One shared crossfade duration and preserved hard cuts | Per-boundary transition data and engine changes |

Ranks 1–3 form the first interaction release after QML parity. Ranks 4–6 follow as separate increments. Ranks 7–10 expand the editing model and should not delay the first release.

## Current behavior to preserve

The current application already supports import and Finder drops, clip selection, frame-quantized edge trims, splitting, clip reordering, removal, rotation, global crossfades, narration placement, looping background music, proxy preparation, undo/redo, native-resolution export, light/dark appearances, and a resizable workspace. These are migration requirements, not missing features.

Source anchors for the Widgets baseline from which the QML implementation was migrated:

- [`CMakeLists.txt`](../../../CMakeLists.txt) separates `isaac_core` from the Widgets executable. The core depends on Qt Core and MLT.
- [`src/timeline_widget.cpp`](../../../src/timeline_widget.cpp) paints text blocks and implements single-clip gestures, ruler seeking, zoom, and scrolling. Trims update their painted geometry during dragging and commit on release. Audio blocks currently select the Audio inspector.
- [`src/main_window.cpp`](../../../src/main_window.cpp) owns the project, undo integration, selection, import, inspector edits, preview rebuilding, and export UI. Editing control is not yet fully separated from Widgets.
- [`src/preview.cpp`](../../../src/preview.cpp) copies decoded RGBA pixels into a `QImage`. Seeking stops the consumer and renders a still through the caller. This is a path to profile; it is not a measured bottleneck.
- [`src/project.h`](../../../src/project.h) represents a sequential video list, one narration file, one looping music file, a shared crossfade, and rotation. It does not represent arbitrary video layers, multiple audio clips, captions, or keyframes.
- [`src/project.cpp`](../../../src/project.cpp) uses `QSaveFile` for manual saves and reads project versions 1–4.
- [`docs/universal-design.md`](../../universal-design.md) defines the existing contrast, keyboard, theme, and responsive-layout requirements.

This baseline comes from source inspection. Playback latency and live assistive-technology behavior need measurement on the resulting QML application.

## Qt Quick adoption decision

### Approaches considered

| Approach | Benefit | Tradeoff | Decision |
| --- | --- | --- | --- |
| Keep refining Qt Widgets | Least immediate migration effort; retains current controls | More custom imperative painting for the desired timeline and interaction work | Keep only during migration |
| Qt Quick/QML with the existing C++ engine | Declarative UI, custom controls, animation, and direct C++ integration across desktop platforms | Requires a new UI and extraction of editing control from `MainWindow` | Selected by the user |
| SwiftUI/AppKit or a web desktop shell | Native Mac integration or familiar web styling tools | Changes platform scope or introduces another integration boundary | Not selected |

QML presents views and temporary gesture feedback. C++ owns committed project state, validation, undo commands, media jobs, and frame calculations. Qt documents this division in its [QML and C++ integration overview](https://doc.qt.io/qt-6/qtqml-cppintegration-overview.html).

Use Qt Quick Controls for the application shell, panels, menus, and standard fields. Expose focused C++ models and commands for clips, selection, playback, and jobs. Keep one authoritative project model; QML must not maintain a second editable copy. Extract the existing editing and undo responsibilities from `MainWindow` into a UI-independent controller before replacing their callers.

Use a dedicated C++ `QQuickItem` bridge for displaying preview frames without sending image bytes through JavaScript. An image-to-texture bridge is a valid first implementation. Native texture sharing is a later optimization if profiling justifies it; adopting QML does not establish hardware decoding or a zero-copy media pipeline. Qt's [scene graph documentation](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html) describes the available rendering integration points.

Generate thumbnails, waveform peaks, metadata, and proxies asynchronously. Prioritize the visible timeline and cancel obsolete work. Bound cache memory, and invalidate cached results when source identity, modification time, stream, or extraction settings change. A rotated or trimmed instance can reuse source-derived data with the correct display mapping. Follow Qt's [performance guidance](https://doc.qt.io/qt-6/qtquick-performance.html) on asynchronous models and simple delegates.

## 1 Filmstrips and waveforms

### Gap and interaction

The current timeline identifies footage by filename and duration. Audio lanes contain labeled blocks without waveforms. The user must play footage repeatedly to identify a shot or a pause.

Show repeated thumbnails across the visible source range of each video clip, with a stable filename strip. Thumbnails represent their position within the clip, including its trim and rotation. At a wide zoom, show several samples; at a narrow zoom, retain one representative image or a compact labeled block. Never allow labels to obscure trim handles or pretend a thumbnail is an exact frame selector.

Show a waveform for original clip audio, narration, and music. Keep a fixed amplitude scale when zooming in time. Render music repetitions using the source's waveform and indicate loop boundaries. Muted material retains a subdued waveform plus a visible mute symbol. Make waveform height independently adjustable from gain; display zoom must not change the audio.

Reserve space immediately while background extraction runs. Use a quiet placeholder, and show a retry action if extraction fails. Keep the clip selectable and editable during preparation. Refresh only the affected visible region when data arrives.

### Acceptance

- At project Fit, a user can distinguish major shot changes and narration pauses without playing every clip.
- Trimming, splitting, reordering, rotation, zooming, and proxy completion retain correct source-to-timeline mapping.
- A 200-clip timeline renders and requests derived media for the viewport plus a small buffer, with a bounded cache rather than one permanent delegate per source frame.
- Missing waveform data is represented as unavailable, not as a flat line suggesting silence. Filenames, numeric timing, and keyboard access remain usable without imagery.

## 2 Responsive navigation and preview

### Gap and interaction

Seeking and basic zoom already exist. The missing behaviors are a distinct skimmer, zoom anchored to the user's point of interest, drag-edge scrolling, and a preview scheduler that can discard obsolete seek requests.

Keep committed playhead position, hover skim position, and selection separate. Clicking a clip body selects it without moving the committed playhead; clicking or dragging the ruler seeks. This is an intentional interaction change after migration parity. A clearly labeled Skimming toggle is initially off. When enabled and paused, hovering over footage previews that position with a thinner, distinct line. Leaving restores the committed playhead frame. Playback, Split, and Insert use the committed playhead; hover alone never redirects an edit. Skimming is suppressed during playback and editing gestures.

Final Cut distinguishes playhead and skimmer for media review; its command targeting differs from this proposal. See Apple's [playback introduction](https://support.apple.com/en-mide/guide/final-cut-pro/veradb8241c/mac).

During scrubbing, move the playhead and time display immediately. Coalesce requests so the newest requested frame wins; stale frames must never replace a newer request. Pause playback when scrubbing begins. On release, present the exact requested frame and resume only if playback was active before the gesture. Hover previews are silent by default. Do not rebuild the edit graph for each pointer movement.

Pinch zooms around the pointer position. Existing zoom buttons and their keyboard alternatives anchor around the playhead. Ordinary horizontal trackpad scrolling pans; a mouse wheel pans vertically where needed and Shift+wheel pans horizontally. Offer a documented modified-wheel zoom equivalent. Fit changes the viewport only. During a clip drag near the timeline edge, scroll progressively while retaining pointer capture; stop on cancel, drop, or pointer-grab loss.

### Acceptance

- Zooming preserves the same timeline frame under its anchor within one frame of rounding; it does not seek, alter selection, or dirty the project.
- Fast back-and-forth scrubbing never displays an obsolete result after the final requested frame.
- Esc cancels a scrub and restores its starting playhead and playback state; hover never creates undo entries.
- Proposed release targets on an Apple silicon Mac: visual pointer feedback within the next 60 Hz display frame, warm-proxy seek latency at the 95th percentile at or below 100 ms, and an exact final frame within 250 ms of release. These are targets, not current measurements.
- Measure a five-minute, 30-clip H.264 vlog with prepared proxies on a local SSD, in a release build. Record hardware, proxy dimensions, cache state, and failures. Cold sources may take longer; show a loading state after 200 ms while preserving the last valid image.

## 3 Precise trims and confident assembly

### Gap and interaction

The current app already trims, splits, reorders, removes clips, and commits a trim on release. It lacks snapping, a two-frame trim comparison, multi-selection, and visible preview of the complete resulting sequence.

Hovering over either edge reveals the appropriate trim cursor and hit region without requiring an earlier selection click. Pressing selects the clip and captures the gesture. For tiny clips, show distinct expanded handles after selection and retain numeric Start/Length fields. A drag updates a temporary edit, the downstream clip positions, project duration, and a trim overlay showing source time and the signed change in frames. The viewer shows the outgoing last frame and incoming first frame while adjusting a cut. This is inspired by Apple's [precision trimming tools](https://support.apple.com/en-au/guide/final-cut-pro/ver1632d3c8/mac).

Keep the main sequence gapless: shortening a clip moves subsequent clips earlier; lengthening it moves them later. Do not silently replace media or introduce a new overwrite mode. During a reorder, draw the dragged selection and an insertion line while showing the resulting order before release. Reject a no-op drop without adding history.

For trims, offer snapping to valid neighboring clip boundaries, the committed playhead, and project start/end. Main-sequence reorders target existing insertion boundaries only, including the sequence end; they never split a destination clip just because the playhead lies inside it. Interior insertion is the explicit operation introduced in rank 5. Use an initial 8 logical-pixel acquisition distance and 12-pixel release distance to avoid flicker. Among valid targets choose the smallest frame displacement, with the playhead winning ties. Show a vertical guide and target label. N toggles snapping when a text field does not own the keystroke; holding Option/Alt temporarily suppresses it. Keep snapping subordinate to source bounds and the one-frame minimum. Apple's [snapping guide](https://support.apple.com/en-ca/guide/final-cut-pro/ver9f7888dc3/mac) is the reference for visible alignment assistance.

Command/Ctrl-click toggles clip selection, Shift-click selects a contiguous range, and clicking empty timeline space clears it. Move selected clips as one ordered group, preserving their original relative order even when initially discontiguous. Delete removes the selected group and closes gaps. Provide equivalent Select Range, Move Earlier/Later, and Remove commands without dragging. Split affects the selected clip under the committed playhead; if none is eligible, disable it with a reason.

Preserve current audio timing during these edits: narration retains its absolute project offset, music starts at zero and loops to the new project end, and original clip audio follows its video clip. Do not infer narration attachments from nearby footage.

Preserve the current global crossfade calculation until rank 10. A short clip may reduce the effective overlap at every active cut. Preview all resulting boundary changes and the total duration during a trim or group edit, and indicate when other fades will shorten. Do not present this as a local-only change.

### Acceptance

- One committed gesture creates one undo command. Esc, lost capture, or a rejected edit restores the complete pre-gesture state and creates none.
- Every trim retains at least one frame and stays within source bounds. Downstream geometry matches the rendered project after release.
- Preserve the existing restriction on splitting inside a crossfade, with an actionable explanation. Snapping and group selection must not bypass it.
- Selection alone does not dirty the project. Undo/redo restores project content and a valid selection without losing the user's viewport.
- A keyboard-only user can identify a clip, adjust its trim by a frame, reorder it, and undo the edit.

Roll, slip, and slide tools follow this first release. They need explicit cut-selection behavior and separate validation rather than hidden modifier gestures.

## 4 Direct audio editing

### Gap and interaction

Narration placement and music gain are numeric inspector controls. The timeline audio blocks select the inspector but cannot be moved, trimmed, faded, or mixed directly.

First expose controls for the existing narration and music sources: drag narration horizontally to change its offset; show an inline dB gain line; and add start/end fade handles. Hovering displays the value, dragging previews the result, and release commits one undo command. Selecting an audio block reveals the same values in the inspector. Provide numeric controls and increment buttons for every operation. Final Cut's [audio fade handles](https://support.apple.com/en-ca/guide/final-cut-pro/ver39580fed/mac) are the reference for direct fade adjustment.

Add gain/mute for clip audio, narration, and music, plus a master output meter. Do not change a source's waveform merely because its gain is reduced. Show persistent clipping indication on the meter and allow it to be reset. Mute is a saved edit; preview-only solo is visibly marked and never silently affects export.

Keep the existing music-to-end behavior as an explicit Loop to end setting. Its outer handles adjust fades, not an imaginary source duration. Arbitrary music trimming and multiple audio clips belong to the later audio model. Narration source trimming can be added with explicit source in/out fields; it must not be confused with moving its timeline start.

### Acceptance

Adjusting narration position, gain, mute, or a fade produces the same audible result in preview and export, survives saving, and undoes in one step. Fade lengths clamp to playable duration. Existing projects retain their original mix. Voice recording, automatic ducking, and noise reduction are later additions; simple manual mixing comes first.

## 5 Source browser and usable ranges

### Gap and interaction

The left Clips list currently mirrors timeline instances. Import immediately appends complete videos, so browsing and deciding what to use already changes the movie.

Create a Media browser for reusable source assets, separate from the sequence. Import adds sources to Media without editing the sequence. A drop onto Media imports only; a drop onto the timeline imports and inserts at the displayed location. An explicit Import and append command preserves the current fast path.

Select a source to open Source mode in the viewer, clearly labeled separately from Project mode. Store a source's own play position and marked range independently of the project playhead. I marks the current source frame as the inclusive start; O includes the current source frame by storing the exclusive end one frame later. Show selected duration, provide Clear Range, and disable additions for invalid or empty ranges.

Append adds the marked range to the end. Insert adds it at the committed project playhead, splitting an existing clip there and moving later clips forward. Preview the insertion and resulting duration before a drag is released. When insertion would fall inside an active crossfade, reject it with the same boundary guidance as Split. Apple's [insert workflow](https://support.apple.com/en-au/guide/final-cut-pro/ver4e2eff6/mac) provides the editing reference.

Add search by filename and filters for video/audio and used/unused assets. Use a usage badge rather than treating a used asset as unavailable. Removing an instance from the sequence leaves its source in Media; removing an unused asset from Media does not delete the file. Removing an asset still referenced in the sequence must explain its uses and offer cancellation.

### Acceptance

One source can supply multiple independently trimmed instances. Source ranges and the asset library survive save/reopen in a new project schema. Versions 1–4 load by deriving assets from their existing paths without changing timing or rotation. Timeline undo and Media import history follow explicit command boundaries. No action deletes original media.

## 6 Recovery and long-running work

### Gap and interaction

Manual save is already atomic and closing an unsaved project prompts the user. There is no recovery journal or relink UI. Export disables the central workspace and menus for the full job, and cannot be canceled.

Write a recovery snapshot after two seconds without a committed edit, with a maximum interval of 30 seconds during continuous editing. Capture committed state only, including untitled projects. Keep recovery separate from the user's explicitly saved file. Show Saving recovery, Recovery saved, or an actionable failure near project status without interrupting editing. A failed recovery write must not mark the project clean.

On restart, offer Recover or Discard with project name and snapshot time. Recovery opens as unsaved work and preserves the original saved file. When sources move, retain offline placeholders with duration and timing. Locate one file or a folder, suggest matching missing files, and require confirmation for ambiguous matches before committing relinks. Do not erase edits because a source is missing.

Export opens a compact panel showing destination, dimensions, frame rate, codec, and the fact that originals are used. Run a frozen project revision in the background. Allow continued editing without changing the active export; label which saved or unsaved revision it uses. Cancel stops at a safe boundary and cleans only that job's temporary output. Publish completed output atomically so cancellation does not destroy an existing destination. On completion offer Reveal in Finder or the platform equivalent, and Open video. Errors retain settings for Retry.

### Acceptance

Test recovery after forced termination, an untitled project, a failed write, and missing sources. Compare exported content against its captured revision while editing continues. Cancel must release workers and media resources without claiming completion or overwriting original media. These require engine/job changes and failure testing, not just a new progress control.

## 7 Connected B-roll and overlays

Status: paused at the user’s request. The V1–V3 tracks and visual-overlay creation/editing controls are removed from the interface. Existing saved overlay content remains compatible. The following design is retained for reference; text and caption attachment stays available.

The current sequence cannot show another video or image above the main picture. Add connected visual clips above the main sequence, with an explicit connection indicator. Dropping into an overlay lane creates an overlay; dropping into the main sequence inserts. Always show which operation will occur before release. This adapts the idea of connected clips described in Apple's [Magnetic Timeline guide](https://support.apple.com/guide/final-cut-pro/intro-to-the-magnetic-timeline-verb8fcfc133/12.4/mac/26.6).

Default to attaching an overlay start to a source frame in a main-sequence clip. Reordering that host moves the overlay with it. Splitting transfers the attachment to the child containing that source frame. A trim that removes the attachment frame clamps the connection to the nearest retained frame and previews that move. Deleting a host includes its attached overlays in the previewed removal and in the same undo command. Offer Detach to project time before deletion when the user wants to keep them. A detached overlay keeps its absolute position during main-sequence edits. Overlay ends may extend past their host, but the first version clamps them to the project duration and shows any resulting trim before commit.

Provide lane visibility controls and clear stacking order, with upper lanes composited above lower lanes. Save visibility changes that affect export; distinguish any preview-only controls. Acceptance requires preview/export agreement, stable attachment behavior through trim/split/reorder/undo, and compatibility with old single-sequence projects. This is a major model and engine extension, not an extra painted lane.

## 8 Titles and captions

Add a timed text lane and a captions panel. Start with manual titles and SRT caption import, correction, and export; automatic transcription is a later optional service or local capability. CapCut's [desktop caption workflow](https://www.capcut.com/help/batch-edit-to-recognize-subtitle) provides the reference for selecting timeline captions and editing them in a dedicated panel.

Clicking a caption selects its timeline interval and corresponding panel row. Double-click or Enter opens text editing. Enter commits a row, Shift+Enter inserts a line break, and Esc restores its previous text. Separate text entry from playback shortcuts so typing Space never starts playback. Provide previous/next-caption commands and a deliberate preview action at the selected caption's start.

Imported and manually entered captions default to absolute project times, shown by a project-time indicator. Ripple edits do not silently shift them. If a duration or order change can invalidate their timing, retain the captions and show a Timing needs review banner. An explicit Attach to clip command can instead bind the caption start to the host source frame using the attachment, split, trim, and deletion rules in rank 7; its duration stays fixed except for a visible project-end clamp. Detach restores project-time behavior at the current position. Manual titles follow the same attachment choices.

Dragging caption edges adjusts timing in frames. Show overlap warnings, constrain minimum duration, and keep imported overlaps visible instead of silently dropping text. Batch style changes affect selected captions; an explicit Apply style to all command affects the full set. Show safe margins in the viewer, support wrapping and fallback fonts, and retain editable text independently of rendered pixels. Offer SRT output and a separate Burn captions into video option.

Acceptance includes multiline and non-Latin text, supported font fallback, undoable batch changes, frame-accurate timing, and preview/export agreement. Timed-text rendering depends on the composition work in rank 7; the first caption release can use one dedicated text layer without arbitrary nested compositions.

## 9 Reframing and keyframes

The current app fits the whole rotated image inside a landscape canvas. Add an explicit project canvas selector for landscape, portrait, and square with numeric dimensions, while preserving the existing auto-resolution mode for old projects. Changing aspect ratio shows the proposed framing and never silently crops every clip.

Selecting Transform overlays handles in the viewer. Drag the image to reposition, corners to scale uniformly, and a rotation handle to rotate; the inspector exposes identical values and Reset. Separate Fit, Fill, and Crop. During a drag, display alignment guides and exact values. Esc restores the previous transform. Changes affect preview and export through the same engine data, rather than a QML-only visual effect.

Next add a keyframe diamond beside position, scale, rotation, and opacity. The first explicit keyframe enables animation for that property. A later edit at another playhead frame adds or updates a keyframe there. Without keyframes, edits remain static. Provide previous/next keyframe, delete, and Linear or Ease interpolation. CapCut documents animation of these properties and movement of keyframes in its [keyframe guide](https://www.capcut.com/resource/how-to-add-keyframes-in-capcut). For IsaacVideoEditor, store each instance's keyframes at source timestamps, an independent proposed data-model choice. Splitting copies its property curves to both children; trimming changes the visible interval without deleting hidden keyframes. Evaluate the retained curves at the newly visible source times so motion stays continuous. Moving a keyframe changes its source timestamp and therefore its displayed timeline position within that instance.

Acceptance requires identical framing and interpolation in preview and export, correct split/trim behavior, undoable gestures, and preservation of legacy rotation when loading old projects. Color grading, masks, tracking, and motion presets follow these basic controls.

## 10 Transitions at individual cuts

Retain the current global crossfade as a convenience. Add a selectable transition block at a cut, with its own type, duration, and Remove action. Dragging a preset onto a cut previews only that boundary. Dragging a transition edge changes duration and displays the exact overlap and resulting project duration before commit. Other cuts retain their settings.

Clamp overlap to valid playable ranges and explain when the requested length cannot fit. Splitting creates a hard cut as it does today; a transition is added only through an explicit command. Roll/slip tools and transitions must use the same cut model when those tools arrive. Acceptance covers two neighboring transitions on a short clip, no source overrun, one-step undo, and matching preview/export timing.

## Rules shared by every interaction

- **Transaction:** press or field edit begins a temporary change; movement previews it; release or Apply commits once. Esc, canceled dialogs, lost pointer capture, or validation failure restores the original. A zero-change gesture creates no history. Transient feedback never enters recovery snapshots.
- **Time:** keep frame arithmetic in C++; retain the current 60 fps, exclusive-out convention during migration. A variable-frame-rate project system is separate scope. Source timestamps and proxies must map consistently to timeline frames.
- **Feedback:** selection, focus, disabled state, snapping, missing media, and progress each need distinct shapes or text as well as color. Preserve the last valid preview while a new result loads. Show why an action is unavailable beside the action.
- **Access:** retain numeric and single-click menu/button alternatives to drags, complete keyboard operation, visible focus, meaningful accessible names/values, and screen-reader announcements for committed edits. Moving from native Widgets to custom QML requires rebuilding and testing these semantics.
- **Layout:** preserve both themes, system fonts, readable contrast, resizable panels, the 760×560 workspace, and usable 24 pt text. Compact mode may move the inspector into a tab; it may not hide core editing commands.
- **Motion:** playheads, trims, and dragged media track input immediately. Use short optional animation for panel changes and successful completion, respect reduced motion, and never animate between exact frame positions in a way that conceals the selected frame.
- **Resource priority:** active playback and the newest seek take precedence over thumbnail, waveform, or proxy work. Export uses a frozen revision and originals. Background work cannot mutate a project or revive a canceled gesture.

## Delivery order and completion gates

1. Extract the editing controller and add the QML shell, C++ view models, standard controls, and preview bridge. Preserve current features, project versions, output behavior, and keyboard alternatives. Keep the existing app available for comparison until parity; remove its production UI path once parity is verified rather than maintaining two interfaces indefinitely.
2. Deliver ranks 1–3 in small increments: derived-media visibility, preview/navigation, then precision gestures and selection. None requires arbitrary video layers or a new media engine.
3. Add direct audio controls, source browsing, and recovery/export in separately reviewable increments. Introduce project schema changes only when needed, with fixtures proving old projects retain their behavior.
4. Add layers, text, transforms, animation, and local transitions after their corresponding models, validation, serialization, preview, and export behavior exist.

For each increment, verify the user action and its cancellation, undo/redo, save/reopen where relevant, preview/export agreement, keyboard and pointer alternatives, both themes, large text, and stale asynchronous results. Reuse relevant core tests; replace Widgets-specific interaction tests with equivalent Qt Quick interaction tests as their surfaces migrate. Do not treat screenshots or a successful build as proof of interaction correctness.

Before the first QML release, perform live VoiceOver testing on Mac and measure the preview targets above. Verify Windows and Linux builds on those systems before describing them as supported releases. Record the test machine and fixture characteristics alongside performance results.

Implementation details left to the builder include component names, model role names, cache budgets selected through profiling, and the initial preview bridge implementation. The selected stack, source-preserving behavior, transaction rules, prioritized interaction outcomes, and compatibility requirements govern those choices.
