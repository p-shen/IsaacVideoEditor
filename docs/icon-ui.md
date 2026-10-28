# Compact icons and clearer controls

The approved UI audit is implemented in the existing Qt Quick workspace with the Graphite + iris palette and system text sizing. No dependencies were added. The original SVGs in `qml/icons/` share a 24-unit outline grid and are tinted through Qt Quick Controls’ icon renderer.

## Controls

- Icon-only: Play/Pause, Undo/Redo, clip up/down, Remove from edit, rotate left/right 90 degrees, Clip audio, Mute, Bold, the clickable color swatch, audio file actions (ellipsis), Edit caption (pencil), Reset transform/clipping (circular arrow), Cancel canvas preview and Dismiss. Add caption keeps its label in the empty state and becomes a plus button once captions exist.
- Icon plus label: Split (scissors), Add text, Snap (magnet), Transform, Solo (headphones), Attach/Detach (link/unlink), and Mark timing reviewed (check). Scope-sensitive actions such as Apply to all cuts and Apply style to all captions retain their labels.
- Keep labels for Skim, inspector tabs, Shadow, 180-degree rotation and form fields. Unit suffixes shorten Start, Length and Volume; timing uses seconds, scale/opacity/position use percentages, and frame precision uses integrated steppers.
- The timeline toolbar groups history, editing and view controls. Delete text is the trash button beside the selected overlay in the Text inspector.
- The empty preview offers an **Open Project** button for saved `.isaac.json`/`.json` projects and “or drag / drop videos to start”. Both disappear when any videos are present, and return if the edit becomes empty. Playback controls are shown once videos are present. Empty drop targets retain short cues; the Clip inspector shows only “Select a clip to edit” until a clip is selected.
- Recovery status and missing-media tools live in **File → Recovery and offline media…**, rather than a permanent workspace strip.
- There is no project header. The native window title shows the project name and saved/unsaved state. Saving clears any old notice instead of displaying a persistent success banner. Export remains available only through **File → Export…** and Command/Ctrl+E; **File → Import** and Command/Ctrl+I add more videos after the preview prompt disappears.

## Inspector interactions

The six sections are Clip, Audio, Transitions, Text, Transform and Captions. Each owns its scrolling area, so a first visit begins at the top and returning preserves that section’s position. Named expandable groups keep precision, animation, appearance effects, attachment, guides and file/bulk actions available with short active-state summaries. Empty Text and Captions panels offer creation actions directly.

Audio edits one source without unrelated or duplicate forms. Transitions require a selected cut for presets, and bulk duration changes wait for explicit Apply. Transform keeps basic reframing prominent; custom canvas fields appear only for Custom, and pending Apply/Cancel stays outside collapsed canvas settings. Numeric focus changes preserve precise positions and mixed caption styles; stale edits cannot commit to another clip or audio source.

## Feedback and accessibility

Icon buttons have independent accessible names, keyboard focus rings and tooltips on pointer hover or keyboard focus. Snap and Bold expose native checked state and add a visible underline. Text entry keeps its original keyboard handling; Space activates focused buttons and combo boxes, while timeline/workspace focus retains playback. The clip list and numeric fields remain alternatives to dragging.

Dropdowns use the theme's panel background, readable text, tinted selection rows, an accent outline and a visible popup border. Arrows follow the theme's secondary text color. Application menu highlights also follow the selection palette, including in dark mode. Text selection retains its accent fill and contrasting foreground.

Filmstrips show neutral placeholder tiles and one compact preparation indicator per clip. Missing media and unavailable waveforms show warning symbols instead of repeated text. Hover a clip/audio lane or focus the timeline for full details; keyboard help includes narration and music warnings even when no clip is selected. Offscreen status reads completed cache state rather than interpreting absence from the visible waveform map as loading. Valid silent waveforms remain drawn as signals rather than warnings.

Compact inspector width grows with text size. Keyboard focus automatically reveals a control within nested scrolling areas, including lower formatting controls and wrapped toolbar actions. Short windows retain workspace scrolling.

## Verification

The inspector simplification passed all 26 CTest suites on macOS with Qt 6.12/MLT 7.42 before the icon follow-up. UI regressions exercise the header-free project prompt lifecycle and JSON project opening, menu export availability, saved/unsaved window titles, compact icon sizing/names, keyboard Snap/Undo/nudges/Bold/delete, empty-inspector disclosure, preserved edit bindings and gesture behavior, and actual focused-control visibility at 760×560 with 24 pt text. Timeline tests cover hover/keyboard details, audio failures, completed offscreen waveforms and uniform placeholder tiles. Contrast checks cover primary labels, normal/highlighted/pressed dropdown rows, menu highlights, dropdown arrows and text selection in both themes. Dropdown tests exercise keyboard selection with string and object models and reject QML warnings.

The current inspector screenshots are saved under `docs/inspector-ui/` for all six modes in light, dark and large-text layouts. The inspector regression suite covers independent scrolling, no-selection preset guards, explicit bulk Apply, stale-selection protection, focus-only precision preservation, mixed caption formatting, valid rounded fade boundaries, canvas preview cancellation and complete control visibility within panel/window bounds.

Earlier screenshots are saved under `docs/icon-ui/`: normal light/dark layouts, the empty import preview, selected text, the compact Audio/Text inspectors and scrolled toolbar, and open dropdowns in both themes. Native app checks also inspect icon rendering, audio controls and selected text formatting.

An independent read-only review found missing keyboard audio details and false offscreen loading feedback. Both have failing-then-passing regression tests. Its compact-layout verification gap is closed by focused-control visibility assertions and additional screenshots. Live VoiceOver and Windows/Linux hardware checks remain unverified.

The icon follow-up rebuilds successfully and passes the inspector, shell and strict normal/large-text layout checks in both themes. The full workspace run passed 24/26 suites; `media_cache_test` and `quick_timeline_test` failed media-frame alignment/thumbnail or timeline checks and remained failing in isolated reruns. No media-engine, timeline or test source changes were made in this icon pass.
