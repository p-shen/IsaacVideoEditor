# Descript styling implementation plan

> **For agentic workers:** Use superpowers:executing-plans.

**Goal:** Apply the approved Descript preview to the migrated editor.
**Architecture:** Retain the C++ application palette as the color source shared by QML and the painted timeline. Add a small local QML theme and reusable visual controls; bundle Plus Jakarta Sans and preserve the system text size.
**Tech stack:** C++17, Qt 6.12 Widgets/native dialogs, Qt Quick/QML, MLT.
**Spec:** `../specs/2026-10-26-descript-style.md`

## Constraints

Keep panel placement, compact layout, keyboard focus, text scaling, all existing editing behavior and initial light/dark palette. No engine or project-format changes. This is a non-Git folder; work in an isolated copy and compare baseline hashes before delivering only changed files.

## Review focus

Bundled font loading without installation; live appearance changes; readable foreground/selection colors; large-text inspector access; preserved timeline gestures and text-field shortcut isolation.

## Task 1: Shared Descript styling

Files: `src/theme.cpp`, `qml/{Main,Inspector,Button,Theme,Pane,TextField,TabBar,TabButton,ComboBox}.qml`, `qml/qmldir`, `qml/qml.qrc`, `assets/assets.qrc`, bundled font/license, `CMakeLists.txt`, `tests/theme_test.cpp`, documentation/screenshots.

- [x] Extend existing theme checks for the new initial palette, bundled font availability and foreground contrast. Verify the font check fails before implementation.
- [x] Bundle the font, extend the palette roles and apply it without changing the user's text size.
- [x] Apply shared QML tokens and reusable backgrounds, tabs and fields; soften selected clips; add panel gutters and viewer space without changing handlers.
- [x] Build and run the existing full suite; inspect light/dark and large-text captures, plus a native Mac render.
- [x] Review the scoped changes, check original-file hashes, deliver only changed files and rebuild the actual application.
