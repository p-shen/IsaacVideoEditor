# Caption Word Formatting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Format selected caption text with color and bold while retaining plain text editing and SRT exchange.

**Architecture:** Add validated style ranges to TextOverlay, reuse the shared QTextLayout renderer, and expose selection formatting through the existing caption editing gesture. Extend the current caption editor without replacing it with HTML.

**Tech Stack:** C++17, Qt Quick/QML, QTextLayout, existing MLT compositor; no new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-20-caption-word-formatting-design.md`

## Global Constraints

- Preserve the 60 fps timebase, original media and existing user changes.
- English, Simplified Chinese and joined emoji use grapheme-safe UTF-16 ranges.
- Enter commits one editing gesture; Escape cancels it. Recovery excludes drafts.
- SRT remains plain text plus timing. Version 9 only when word formatting exists; load versions 1–9.
- Reuse current controls, rendering and undo paths; no new dependencies.

## Review Focus

- Text replacement and insertion must retain surviving formatting without splitting joined emoji.
- Uppercase expansion and Word Pop must map original ranges to displayed characters.
- Cache invalidation must reflect color and bold range edits at the same frame.
- Focus transitions to formatting controls must preserve selection and cancellation.
- Invalid persisted ranges must reject the project without loading partial data.

### Task 1: Style ranges and persistence

**Files:** `src/project.h`, `src/project.cpp`, `src/text_style.h`, `src/text_style.cpp`, `tests/caption_test.cpp`.

**Interfaces:** `TextSpan { int start, length; QString color; std::optional<bool> bold; }`; `TextOverlay::spans`; `validTextSpans(const TextOverlay &)`, `setTextRangeFields(TextOverlay &,int start,int end,const QVariantMap &,QString *)`, `replaceTextContent(TextOverlay &,const QString &)`, `textRangeFields(const TextOverlay &,int start,int end)`.

- [x] Add failing range, grapheme, replacement, persistence/version and malformed-data tests; build/run caption_test to observe failure.
- [x] Implement the helpers and project validation/save/load. Include spans in equality and use shared replacement for title content changes.
- [x] Run caption_test and existing project/text checks.

### Task 2: Renderer and controller

**Files:** `src/text_renderer.cpp`, `src/caption_controller.cpp`, `src/editor_controller.h`, `src/editor_controller.cpp`, `tests/text_overlay_test.cpp`, `tests/caption_controller_test.cpp`.

**Interfaces:** `styleCaptionText(int start,int end,const QVariantMap &fields,bool editing=false)`, `captionTextStyle(int start,int end) const`, `chooseCaptionTextColor(int start,int end,bool editing=false)`.

- [x] Add failing tests for style pixels/cache, uppercase/Word Pop, draft Undo/cancel/save/recovery and SRT text preservation.
- [x] Apply font weight before layout, resolve fill colors during drawing, include ranges in layout keys and remap ranges for rendered substrings.
- [x] Route selected-text styling through the existing caption gesture and expose style state; reject invalid selections and values atomically.
- [x] Run affected controller/rendering tests and verify a frozen exported revision contains selected-word color.

### Task 3: QML interaction and delivery

**Files:** `qml/CaptionInspector.qml`, `tests/qml_shell_test.cpp`, `docs/captions.md`.

- [x] Add failing interaction coverage for selection, formatting control focus, keyboard bold, commit, cancel and switching captions.
- [x] Add the Selected words controls with accessible names, persistent selection, focus-safe editing and Command/Ctrl+B.
- [x] Build, run the affected QML checks, full CTest suite and a live app flow using temporary media/project files.
- [x] Run one fresh review, fix substantive findings, update this checklist and report actual checks and limits. Keep changes uncommitted for review.

## Execution record

- Work in the current checkout to preserve the user's uncommitted editor work. No source changes from the subtitle trial.
- Native execution; one fresh review at the end. No automatic commits, branch changes or new dependencies.

## Results

- Full build succeeded; full CTest suite passed 34/34 in 242.07 seconds before concurrent caption-timing performance work arrived.
- Model/controller/renderer regressions cover bilingual and emoji ranges, persistence, SRT, Undo/cancel/recovery and frozen styled MP4 output.
- Native macOS production-QML checks pass for selection, keyboard bold, Tab/Backtab, color dialog acceptance/rejection, Escape, focused Save and Undo. Automated dialog checks use Qt dialogs; the macOS system color panel was not manually verified.
- Fresh review found Tab insertion and focused-save gesture issues; both were reproduced and fixed. No deferred review findings.
- Existing user work is preserved; changes remain uncommitted. Concurrent performance edits in the same checkout are outside this feature. Linux and Windows were not checked.

- Final focused regression run after the review fixes and native harness correction passed 4/4 targets in 88.62 seconds; whitespace checks pass.
