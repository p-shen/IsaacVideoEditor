# Source Waveform Implementation Plan

> Implement inline using superpowers:executing-plans and test-driven development.

**Goal:** Display audio while previewing and trimming a source clip.

**Architecture:** Add CachedWaveform beside CachedFrame, backed by MediaCache. Render it inside the existing SourceControls slider, with range and cursor overlays.

**Tech Stack:** C++17, Qt Quick/QML, existing FFmpeg cache.

**Spec:** ../specs/2026-11-02-source-waveform-design.md

## Constraints and review focus

Keep source timing at 60 fps, extraction off the UI thread, and source media unchanged. Avoid new dependencies and persistence changes. Check silent/no-audio sources, switching sources, fixed amplitude/time mapping, keyboard/Escape behavior, and short/large-text layouts.

## Task 1: Source waveform

- [x] Add QML shell tests using synthetic tone/silence media; prove the waveform is initially absent.
- [x] Add CachedWaveform in src/preview_item.h/.cpp, exposing cache, source, duration, color, and state. Refresh from MediaCache::changed on the GUI thread; draw 20 peaks per source second with fixed amplitude.
- [x] Register it in src/qml_registration.cpp and add a waveform background with range/cursor markers to qml/SourceControls.qml. Preserve the slider's existing interaction and cancellation.
- [x] Run waveform/source interaction checks, build the app, run CTest, and inspect captures in both themes and compact/large-text layout.
- [x] Review the diff and leave the changes ready for the user.

## Verification

The new waveform test first failed because the waveform was absent, then passed with the implementation. Five focused source checks passed. The Release build and all 34 CTest suites passed on arm64 macOS 15.7.3 with Qt 6.12/MLT 7.42. Native Mac window checks covered the waveform, keyboard seeking, canceled scrubbing, both themes, and compact/large-text layout; captures were inspected. An independent code review found no actionable issues. Linux and Windows were not checked.

Used a clean temporary build because regeneration stalled in the existing build directory. The verified app bundle was copied to build/IsaacVideoEditor.app. Source changes remain uncommitted.

## Task 2: Source layout follow-up

- [x] Add a failing source-layout check for preview dimensions, compact controls, large-text focus, and restoring the Project inspector.
- [x] Consolidate SourceControls into a waveform and wrapping trim toolbar; move frame stepping into the shared playback row and suppress redundant informational rows.
- [x] Hide the project inspector in Source mode and constrain compact sidebar width in Main.qml.
- [x] Verify focused source/QML interaction tests, native captures in both themes and compact/large-text sizes, and the full CTest suite. Review the final change and update the app bundle without interrupting the user's unsaved editing session.

Follow-up verification: the initial layout assertion failed against the old preview width. Additional failing checks reproduced compact Inspector navigation, large-text Media button overflow, and clipped playback controls; all pass with the fixes. Native sizing checks wait for the rendered layout, and coordinate clicks synchronize with a displayed frame. The independent review's Inspector-selector finding was fixed. Both themes and compact/large-text captures were inspected using the native Mac renderer.

The final full CTest run passed 34/34 in 195 seconds. After the final preview-panel sizing correction, the full QML shell and inspector suites passed again, and native layout, append/drop, keyboard, and canceled-scrub checks passed. An earlier run had long elapsed-time timeouts; the clean rerun used bounded test timeouts and prevented idle sleep. Concurrent changes to quick_timeline.cpp and quick_timeline_test.cpp were preserved. The app bundle was rebuilt and copied to build/IsaacVideoEditor.app; the open unsaved editing session was left running. Linux and Windows remain unverified.
