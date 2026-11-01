# Text Presets Implementation Plan

> Implement inline using the existing editor and shared text renderer. The user approved all ten styles, independent appearance and animation, and implementation without another approval round.

**Goal:** Ten complete one-click text presets that fit the current inspector, with independently editable typography and animation.

**Architecture:** Extend `TextOverlay`, shared field updates, validation and JSON persistence. Keep preset definitions in `text_style`; the same Qt text compositor renders preview and export. Reuse `TextInspector` for titles and captions.

**Tech Stack:** C++17, Qt Quick/QML, QTextLayout, MLT; no new dependencies.

## Constraints and decisions

- Preserve existing Clean, Playful and Background box appearances and old project files.
- Applying a preset sets typography, decorations and suggested motion in one undo step, preserving words, placement and timing. Editing typography or animation afterward leaves the other unchanged.
- New presets: Classic Outline, Rounded Label, Bold Highlight, Karaoke Highlight, Word Pop, Typewriter, Neon Glow, Editorial Serif, Handwritten Note, Comic Sticker.
- Animation choices: None, Fade, Slide, Pop, Bounce, Typewriter, Karaoke, Word Pop, Pulse. Word animations use clip duration; no speech alignment exists in this editor.
- Bold Highlight emphasizes the last word by default. Uppercase is a render setting and never rewrites the entered words.
- Reuse installed fonts with platform fallbacks, theme controls, focus behavior and compact sample delegates.
- Keep unrelated source-controls and documentation edits intact in this existing checkout.

## Review focus

- Existing project rendering and saved JSON remain compatible.
- Very short cues, multilingual words and emoji remain visible and intact.
- Preview/export and proxy/full-size layouts agree; caches include new layout settings.
- Caption multi-selection and Apply to all retain complete styles and motion.
- The preset popup fits a narrow inspector and large UI text without clipping.

## Tasks

- [x] Add failing tests for complete presets, independent edits, persistence, rendering/motion and inspector controls.
- [x] Extend the text model and shared preset/field APIs; validate optional JSON fields and preserve legacy defaults.
- [x] Extend cached text painting with outlined, highlighted, glowing, rounded and comic appearances plus deterministic per-frame animation.
- [x] Integrate preset samples and animation selection into the existing title/caption inspector; update guide.
- [x] Build, run relevant tests and the complete suite, inspect rendered samples and review the final diff.

## Review and verification notes

- Fresh read-only review found Word Pop could omit words when a cue had fewer frames than words. A failing regression test confirmed this; these cues now show all their text.
- Runtime checks confirmed ordinary mixed Arabic/Latin karaoke bounds. More complex bidirectional typography remains delegated to Qt's existing text layout.
- All 30 text-overlay test cases pass, including legacy pixel comparisons, animations, malformed JSON, persistence, attachments and MP4 output.
- Visual QA covered all ten rendered appearances, light/dark inspector controls, and the narrow inspector/preset popup at 24-point UI text.
- New saves use schema 8 to prevent older builds from silently losing animation settings; schema 1–7 projects remain readable.
- Final macOS validation: complete app/test build succeeded; `ctest --test-dir build --output-on-failure --parallel 2` passed 30/30 targets in 83.58 seconds with normal app-cache access. Whitespace checks passed. Linux and Windows were not run.
