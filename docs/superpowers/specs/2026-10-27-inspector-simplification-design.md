# Simpler inspector

Approved by the user with “agree, implement” after the right-panel audit.

Keep everyday edits visible, put specialist controls in named expandable groups, and preserve all existing editing capabilities and saved-project compatibility. Use the current Qt Quick components, theme tokens and controller APIs, with no new dependencies.

- Shared expandable groups show a short summary, remain keyboard operable and retain important active-state feedback. Numeric fields offer integrated steppers instead of separate rows of step buttons.
- Each inspector section starts at the top on first entry and retains its own scroll position. Rename Fades to Transitions and Visual to Transform.
- Clip keeps Start, Length and quarter turns visible; precision uses integrated frame steppers. Additional rotation options are expandable.
- Audio presents one selected source and its own file, volume, mute and music-loop controls. Fades/timing and preview/levels are expandable. Recent downloads and Mixkit belong in the file menu. Empty sources offer an add action rather than a disabled form.
- Transitions edits an explicitly selected cut in seconds. Frame precision remains available. No preset may silently target the first cut. Project-wide application requires an explicit action and explains that individual overrides are replaced.
- Transform keeps framing, scale and direct preview manipulation prominent. Position, opacity, crop precision and animation are expandable. Canvas setup is a separate named group; custom dimensions appear only for Custom. Apply/Cancel remain visible during a canvas preview.
- Text keeps content, style and compact typography visible. Placement, appearance effects, attachment and guides are expandable. Empty Text offers Add text.
- Captions keeps its list, editing and timing visible. Appearance, attachment and bulk/file actions are secondary. Empty state offers Import captions and Add caption. Units use seconds/percentages by default and frames where precision is explicitly requested. Errors, clipping and timing-review warnings stay visible.

Existing undo/cancel, gesture, selection, export, keyboard and accessibility behavior is preserved. Feature priority is a design judgment, not usage telemetry. No features are deleted.
