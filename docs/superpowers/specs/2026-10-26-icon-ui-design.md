# Icon and interface clarity design

Approved scope: the user asked to implement every recommendation in the UI audit in this chat.

Keep the existing Qt Quick desktop workspace, Graphite + iris theme, system font sizing, editing behavior, native dialogs and media engine. Use one small, bundled SVG icon family; no dependencies.

- Icon-only with tooltip and accessible name: play/pause, undo/redo, clip earlier/later, remove from edit, rotate left/right 90 degrees, frame nudges, bold, color swatch and dismiss. Keep 180 degrees textual.
- Icon plus concise label: Import (arrow into tray), Export (arrow out), scissors Split, Add text, magnet Snap, folder Choose file, Recent and external-link Browse Mixkit. Keep Skim and inspector tabs textual.
- Compact icon buttons must actually shrink from the current 76px minimum, scale with text, keep focus rings and expose checked/pressed state. Tooltips work on keyboard focus as well as hover.
- Timeline toolbar groups history, edit and view controls with spacing. Delete text belongs in the selected text inspector, retaining keyboard deletion.
- One primary import instruction in the viewer, brief drop cues elsewhere. No selected clip means a concise empty inspector rather than disabled properties.
- Short labels and suffix units: Start, Length, Duration (s). Remove duplicate Clip properties and TEXT headings. Preserve crossfade behavior help.
- Replace repeated filmstrip loading labels with neutral tiles and one compact loading indicator per clip. Keep media-missing and waveform-unavailable details accessible through hover and keyboard focus.
- Use a compact waveform status icon. Do not mistake missing or failed media for silence. Preserve current waveform scaling.
- Keep all existing keyboard shortcuts, drag alternatives, undo and redo semantics, responsive panels, light/dark contrast and large text support.

Implementation choice: local SVGs tinted by Qt Quick's IconLabel style renderer; labels and accessible names stay independent. Work in place with a full source backup because this folder has no Git metadata.
