# Remaining editor responsiveness work

The user approved all four remaining improvements after the caption timing patch: incremental caption updates, background recovery saves, asynchronous decoder handoffs, and indexing visible/active timeline content.

Keep the existing C++17/Qt/MLT architecture. A stable QAbstractListModel caches caption records and warnings, emits changed rows, and gives QML cheap count/review properties. A 16 ms single-shot timer combines rapid transient text updates; keyed pending preview edits retain a full update plus the latest change per cue. Commit, cancel, save, and playback preserve the final text state.

Recovery timers dispatch one immutable project snapshot to one worker, serialize once, and publish atomically. New schedules coalesce behind active work; clear/explicit flush invalidate staged publication. Existing explicit flush and teardown remain draining operations for compatibility and reliable lifetime protection.

The existing Preview worker serializes native consumer stop/start, producer handoff, and seeks. Interactive callers use requestPlay/requestPause/requestProducer. Retired producers, profiles, and proxy leases remain owned until disconnect acknowledgment; graph preparation/reuse begins only afterward. Revision guards reject stale callbacks. Synchronous helpers remain for programmatic compatibility and teardown.

A shared immutable half-open interval index handles arbitrary overlaps and returns original input order. Renderers snapshot only active texts/overlays; timeline geometry, painting, and media requests query visible ranges. Preserve containing intervals, equal starts, composition order, backward seeks, still-image ownership, and queued-frame consistency.

Alternatives considered: moving repeated caption-list reconstruction to another thread retains avoidable work and model replacement; unbounded edit callback chains or decoder pools add lifetime/memory risks. Reuse the current single-worker and snapshot patterns with bounded latest work instead.

Preserve 60 fps timing, original media paths, runtime proxy offsets, frozen export revisions, undo, dirty state, cancellation, recovery locking, and older project formats. No dependency changes, implicit commits, or edits to user footage/projects. Work in the existing feature checkout to retain the user's uncommitted changes.
