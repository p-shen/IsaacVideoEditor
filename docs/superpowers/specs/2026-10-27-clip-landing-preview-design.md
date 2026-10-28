# Clip landing preview

Approved in chat: add a landing preview, insertion line, and placement label,
with the committed clip landing exactly where the preview shows.

## Intent

Make timeline clip reordering predictable before the pointer is released.

## Approach

Use the existing transient reordered project and index mapping as the single
source of destination geometry. Draw a translucent outlined landing area over
the moved clip or group, an insertion line at its leading edge, and a readable
"Place before Clip N" or "Place at end" label. Use the same placement wording
in the existing gesture status. Keep labels inside the visible video lane.
Retain the preview's scale and final scroll bounds through the synchronous
commit refresh, including reorders that change crossfade durations.

A marker at the original boundary is insufficient because removing the moved
clips changes their final position. A separate drag calculation risks a mismatch
with the committed order. The approved approach reuses the existing preview.

## Constraints

Preserve current reorder, crossfade, group ordering, scrolling, cancel, and
undo behavior. Add no dependencies. Keep all work in the current checkout so
the existing staged editor changes are retained. Leave those staged changes
and the Git index untouched.

## Verification

Test both reorder directions, unequal lengths, crossfades, selected groups,
cancel/no-op gestures, zoom/scroll, and the final release position. Check the
painted landing marker and outline against geometry after the move is applied.
Build the app and run timeline/controller/QML regression tests. Inspect rendered
light/dark and large-text previews.
