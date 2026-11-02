# AGENTS.md

## Project

IsaacVideoEditor is a local desktop video editor for short vlogs, built with C++17, Qt 6.6+ Quick/QML, MLT 7, and FFmpeg.

- `src/`: project model, editing controllers, media preparation, preview, and export.
- `qml/`: production interface; QML resources are registered in `qml/qml.qrc`.
- `tests/`: Qt Test and integration tests registered with CTest in `CMakeLists.txt`.
- `packaging/`: platform packaging scripts and their Python tests.

See [build setup](docs/building.md), [editing behavior](docs/advanced-editing.md), [performance](docs/performance.md), and [release packaging](packaging/README.md). Check current code when older design notes or plans disagree with it.

## Working principles

- Understand the relevant code before editing. State consequential assumptions and tradeoffs. Ask when uncertainty materially changes the result; use judgment for routine, reversible choices.
- Make the smallest complete change that solves the request. Reuse existing patterns and dependencies. Avoid speculative features, configuration, and abstractions.
- Keep changes focused and match the surrounding style. Avoid unrelated refactoring or formatting. Remove code your changes make unused; mention unrelated problems separately.
- Define observable success, verify it, and continue until the requested outcome is handled. Report what changed, what you checked, and any remaining limits in plain language.

## Build, run, and test

Run from the repository root. With the macOS Homebrew dependencies installed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --parallel 4
open build/IsaacVideoEditor.app
ctest --test-dir build --output-on-failure
```

For a focused check, build the affected test target and select it with CTest, for example:

```sh
cmake --build build --target project_test --parallel 4
ctest --test-dir build -R '^project_test$' --output-on-failure
```

- Add a regression test for a substantive bug fix when practical. Run affected checks during development and the full CTest suite for changes spanning shared editing, preview, or export behavior.
- For QML or interaction changes, also check the affected flow in the running app, including keyboard operation and cancellation. For media changes, use a small synthetic fixture and inspect playback or exported output as appropriate.
- Match verification to the change; documentation-only edits need link and formatting checks. Distinguish checks actually run from checks still needed.
- Follow [the build guide](docs/building.md) for Linux/Windows setup and test-path limitations. Report which platforms were checked.
- For packaging changes, see [packaging checks](packaging/README.md#checks); run the applicable helper tests and validate the resulting package.

## Editor rules

- Preserve original media. Use temporary or synthetic files for checks; never overwrite user footage or saved projects with test output.
- Keep saved projects and exports tied to original source paths and timing. Preview copies and their frame offsets are runtime details; exports use the captured project revision and original media.
- Preserve the 60 fps editing timebase, source orientation, and agreement between preview and export when changing timing or rendering.
- Route committed edits through the existing controller/undo paths. Preserve gesture cancellation, dirty-state tracking, recovery, and loading of older project files when changing editing or persistence.
- Keep decoding, encoding, and other expensive media work off the UI thread. Reuse the existing background jobs and resource budgets.
- Keep generated media, caches, build products, and local `.isaac.json` projects out of commits.

Behavioral principles inspired by the community [Karpathy guidelines](https://github.com/multica-ai/andrej-karpathy-skills/blob/main/CLAUDE.md).
