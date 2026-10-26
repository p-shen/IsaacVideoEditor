# Text overlays

The user approved the interactive text-overlay mockup and explicitly authorized implementation after the QML migration, without another approval round. Reference: `/Users/Peter/.codex/visualizations/2026/10/26/01a17689-3355-72d3-a476-81080201dadb/text-overlay-timeline-v3.html`.

Support manually authored titles and location/description labels over footage. The example opening title is “Adventure Park Day with Isaac and Daddy”. Add text at the playhead and delete the selected text using buttons beside the timeline. Support multiline text, clean/playful/box presets, font, numeric size, color, bold, shadow and optional fades. Select and drag text in the viewer. Drag timeline text blocks to move them and their edges to change duration; show frame-accurate timing in the timeline and a numeric duration field. No text-start field or separate playback-position slider. Preserve existing video editing, proxies, original-resolution export, older project loading and undo/redo.

Use existing C++/MLT project and command architecture with QML controls. A shared Qt text renderer must composite text into MLT frames used by both preview and export; a viewer-only QML overlay would omit exported titles. Store positions as normalized canvas centers, font sizes relative to 1080p, and timing as 60 fps frame intervals with exclusive end. Layer order follows project order. Editing video retains absolute overlay timing; rendering clips overlays to the current video duration without deleting their saved data.

Approaches considered: Widgets implementation duplicates work after migration; independent QML text rendering risks preview/export differences; shared C++ rendering with QML editing preserves identical layout across output sizes. The latter follows the migration architecture and approved feature requirements.

The project is not a Git checkout. Work in an isolated copy and preserve the concurrent Descript styling update when delivering. No automatic captions or complex animation/keyframe system.
