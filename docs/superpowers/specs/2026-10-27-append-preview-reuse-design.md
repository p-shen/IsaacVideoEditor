# Append preview reuse

Appending clips should prepare new media without reopening unchanged video decoders or replacing every visible filmstrip thumbnail. Existing cached preview movies remain reusable.

Reuse prepared video producers from the previous, stopped playback timeline when clip identity, source file identity, trim, audio controls, rotation, and render profile still match. Build the new timeline's transitions, audio tracks, overlays, and captions normally. Register reused transform state with the new timeline so live visual edits continue to work. Keep the previous timeline alive until the replacement is connected. Export continues to build independently.

For timeline filmstrips, choose an existing cached or pending thumbnail inside each tile's source interval before requesting its midpoint. Exact thumbnails used by trim comparison remain exact. Keep automatic timeline fitting and enforce trim, rotation, and source identity boundaries.

Alternatives considered: incremental playlist mutation would require separately updating transitions, audio duration, and overlay timing; stopping automatic fitting would change navigation behavior. Producer and thumbnail reuse avoids those changes.

Constraints: use existing Qt/MLT dependencies; preserve project files, undo/redo, output sizing, export, and unrelated local edits. Reuse must be invalidated when source contents or the playback profile changes. No cache clearing or new preferences.

Validation: failing regression tests for decoder reuse on append and proxy replacement, source/profile/audio invalidation, transform editing after reuse, cached/pending thumbnail reuse, and fit-induced filmstrip requests. Then build the application and run the existing suite.
