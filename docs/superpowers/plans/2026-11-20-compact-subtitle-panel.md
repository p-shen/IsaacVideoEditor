# Compact Subtitle Panel Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans. The user's “yes, implement, and push to main after done” approves the compact proposal and execution.

**Goal:** Make local subtitle generation concise and usable in a narrow inspector.

**Architecture:** Keep the existing controller/model/job interfaces. Change only the QML presentation, reuse house buttons and add one gear icon. Preserve generation, download, cancel, confirmation and Undo behavior.

**Tech Stack:** Qt Quick/QML, existing Qt Test controller integration.

## Approved design

The proposed compact inline panel is preferred over a separate dialog (extra navigation) or an icon-only toolbar (unclear primary actions). Keep a Model selector and ready/download indicator beside an accessible gear button. Use short icon buttons: Generate/Retry, Download, Cancel, Import and Reveal. Show one status line and progress only while working. Put CPU mode, engine/model/device details and logs inside the gear popup. Remove introductory prose and the always-visible output path; retain actionable errors and warnings. Completed output remains available when import is unavailable.

## Constraints and review focus

- No transcription/backend, project format, timing or import-policy changes.
- Keep keyboard access under macOS text-controls-only Tab behavior; Escape closes options and cancels the save dialog safely.
- Missing models, missing engine, download/check progress and failures remain understandable.
- Narrow panels and larger text must wrap actions without clipping.
- Retain accessible names/tooltips and display diagnostics as plain text.

## Task 1: Compact generation controls

**Files:** `qml/SubtitleGenerator.qml`, `qml/icons/settings.svg`, `qml/qml.qrc`, `tests/subtitle_controller_test.cpp`, `docs/captions.md`.

**Interfaces:** Consume the existing subtitle controller and SubtitleModels properties; preserve their public invokables and object names for existing actions. Add `subtitleOptionsButton` and `subtitleOptionsPopup` for option disclosure.

- [x] Update keyboard regression to open CPU options through the gear, toggle CPU without dirtying the project, close with Escape and cancel the native save flow.
- [x] Add a real-controller UI regression for narrow layout, generation cancellation preserving an existing output, completed result import and Undo.
- [x] Observe the expected failures, then implement the compact panel with the existing components.
- [x] Run focused checks, rebuild the app, inspect the affected UI flow and run the full CTest suite.
- [x] Request one independent review, fix material findings and update usage documentation.
- [x] Commit and push the verified change to main without overwriting newer remote work.

## Verification

- Release app rebuilt at `build/IsaacVideoEditor.app`. Final stable suite: 39/39 CTest targets passed in 224.33 seconds on macOS.
- Native Mac keyboard checks passed for model choice, gear/CPU options, popup placement, Escape/save cancellation, active-job Logs, cancellation preserving existing output, import/Undo, and retained Import/Reveal navigation.
- Production-themed screenshots inspected at a 300 px panel width with 20 px text. SVG/resource references and whitespace checks passed.
- One independent review found two keyboard gaps; both were fixed with failing-then-passing regressions.
- Windows/Linux interaction checks and real-model transcription were not repeated for this presentation-only change.
