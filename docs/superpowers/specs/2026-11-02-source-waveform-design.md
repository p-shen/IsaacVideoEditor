# Source viewer waveform

Show the selected source's audio waveform beneath its video while previewing and marking in/out points. The user confirmed the Source viewer as the intended view.

Use the existing source position slider as the waveform surface: display original-source peaks, mark the selected range, and show the cursor across the signal. Keep frame seeking, keyboard operation, and canceled scrubs on the existing controller paths. Loading and unavailable audio must be distinguishable from silence. Use existing theme colors and background MediaCache extraction; preserve original media, the 60 fps timebase, and saved project/export behavior.

Approaches considered: a separate waveform would duplicate seeking and cancellation; a waveform slider background reuses these interactions and takes less vertical space. Choose the slider background. A small painted item reads the existing cache on the GUI thread and draws cached peaks on the render thread.

Verify real audio with loud and silent sections, range and playhead alignment, source switching, missing audio, keyboard seeking, Escape cancellation, and normal/compact layouts in both themes.

## Layout follow-up

The user reported that Source editing became crowded. Inspection of the running app confirmed that separate action/status rows consume preview height and the unrelated project inspector occupies the right side. Consolidate In/Out marking and numeric fields, reset, selected duration, and append/insert into one wrapping toolbar. Keep playback, frame stepping, viewer switching, and time together. Hide the project inspector while viewing a source, retain its state for the Project viewer, and limit the Media sidebar width in compact Source mode. Keep the project timeline available for insertion/drop context.

Use existing controls and icons. Preserve all editing, keyboard, cancellation, and waveform behavior. Normal desktop Source mode should provide at least a 900 px wide, 280 px tall preview at 1320×860; compact controls must fit horizontally and remain reachable with large text and keyboard focus.
