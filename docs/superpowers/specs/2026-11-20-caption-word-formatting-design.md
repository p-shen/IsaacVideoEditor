# Selected caption text formatting

## Scope

Add color and bold formatting to selected text in the existing Captions editor. English, Simplified Chinese and inline emoji remain plain editable text. The user has validated local SRT generation, import and emoji display, and requested implementation of the next agreed feature: selected-word color and bold.

## Interaction

While editing a caption, selecting text enables a compact Selected words row with the existing color-picker pattern and a bold toggle. Keyboard selection, Tab to controls, and Command/Ctrl+B work. Enter commits the editing session in one Undo step; Escape cancels both content and formatting. Moving to another caption commits the current session. Appearance continues to style whole captions; word overrides take precedence. Video preview shows the selected text formatting.

## Representation and compatibility

Store non-overlapping UTF-16 ranges with optional color and bold overrides on the shared TextOverlay model. Endpoints must be Unicode grapheme boundaries, so Chinese and joined emoji remain intact. Text edits retain formatting on surviving text and inherit formatting when replacing text wholly within a styled range. SRT remains plain text plus timing. Projects containing word formatting use version 9; unformatted saves remain version 8. Load versions 1–9 and reject malformed ranges atomically.

## Rendering

Use the existing QTextLayout compositor for both preview and export. Bold must affect shaping and wrapping; cache keys include ranges. Uppercase expansion, typewriter visibility and Word Pop must map styles to the displayed text correctly. Existing animation emphasis may temporarily override selected-word color. Keep existing unformatted rendering unchanged.

## Approaches

Extend Captions and its shared renderer (selected): reuses undo, persistence and output. Separate manual text overlays require duplicate timing and correction work. An HTML editor would replace the existing plain-text workflow and introduce unnecessary conversion.

## Verification and limits

Tests cover overlapping styles, bilingual/emoji boundaries, text replacement, invalid project data, save/load, SRT, Undo/cancel/recovery, cache invalidation and preview/export pixels. Check keyboard editing and cancellation in a running app, plus the full CTest suite. No transcription integration, translation, word timing, per-word backgrounds or emoji picker.
