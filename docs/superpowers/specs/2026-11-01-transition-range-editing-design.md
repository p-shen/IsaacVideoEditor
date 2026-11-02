# Transition range editing

## Intent and agreed behavior

Make the intended transitions easy to target through the GUI. The user chose
**select, configure, apply**. The earliest and latest selected clips define the
range, including clips between them that were not individually selected.
For A–B–C–D–E–F, selecting B and E targets B→C, C→D, and D→E; A→B and E→F
retain their transition settings and overlap durations.

## Approaches

Considered explicit configuration followed by Apply, immediate updates to a
selected range, and choosing a preset before targeting clips. The user chose
explicit Apply: it makes the scope of a bulk edit reviewable before replacing
existing transitions. Use the existing timeline, inspector, preset catalog, and
controller/undo path. No new dependencies or rendering path.

## Proposed interaction details

These details are proposals, not additional user decisions:

- Add a **Transitions…** action beside the timeline and in the Clip menu, enabled
  when at least two clips are selected. It opens the existing Transitions
  inspector with the selected range as its explicit target.
- The inspector also offers **Selected range · N cuts** alongside individual
  cuts when a valid range exists. An individual cut remains selectable directly
  on the timeline. Range editing must not silently fall back to a previous cut.
- Show endpoint clip numbers and filenames, the included cut count, and that
  applying replaces the transitions inside the range. Highlight the entire
  affected span and its cuts while configuring. This indication is distinct
  from ordinary clip selection and does not broaden move/delete operations.
- Stage transition type and duration in seconds, retaining frame precision at
  60 fps. Uniform settings populate the fields; differing settings show
  **Mixed**. Require concrete type and duration before enabling Apply. Choosing
  **Cut** removes the enclosed transitions and requires no duration.
- **Apply to N cuts** changes the range in one operation and creates one Undo
  entry. Configuration alone changes neither playback nor dirty state. A
  no-op Apply creates no Undo entry.
- Changing the selection or switching targets discards unapplied configuration
  and reloads the new target. Escape cancels configuration. Reordering,
  removing, loading, and undo/redo invalidate stale drafts, including edits in
  a focused duration field. Never apply a draft to a different clip pair.
- Retain existing immediate individual-cut editing, timeline resizing with
  cancellation, the draggable Dissolve preset, and the separate all-cuts action.
  The range Apply button appears only for the range target.

## Timing and implementation constraints

Apply to a copy of the current project, validate the complete range, and commit
once through the controller. Evaluate the range together so results do not
depend on the order in which cuts are processed. Preserve outside overlaps;
short clips constrain the enclosed transitions using the existing playable
media rules. Report the number applied and shortened, with Undo available.

Reuse existing overlap timing, audio crossfades, preview/export behavior,
original source paths, persistence, and legacy-project handling. A longer
overlap can shorten the project as it does today. Draft UI state is not saved.
Visual preset thumbnails, new effects, transition alignment controls, and a
separate preset browser are outside this focused change.

## Observable success and verification

Cover adjacent clips, sparse endpoint selections, reverse selection order,
mixed existing settings, removal via Cut, short clips, and unchanged outside
overlaps. Verify staging leaves the project untouched, Apply commits once,
Undo/Redo restores the complete operation, and stale drafts cannot apply.
Check legacy projects and save/load. Exercise the running app with keyboard
navigation, Escape, target changes, narrow layouts, and large text. Run the
affected model/controller/inspector/timeline tests and the full CTest suite;
inspect a small synthetic playback/export fixture if timing code changes.
