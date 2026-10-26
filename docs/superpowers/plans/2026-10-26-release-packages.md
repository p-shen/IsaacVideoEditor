# Release packages implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Produce self-contained downloads only on GitHub Releases, with optional manual validation.

**Architecture:** Three standard hosted jobs build and package native dependencies. A gated upload job attaches all validated packages to the existing release. Relative runtime discovery supports portable binaries without changing developer builds.

**Tech Stack:** CMake, C++17/Qt 6.6+, Python standard library, Bash, GitHub Actions, Qt deploy tools, linuxdeploy.

**Spec:** docs/superpowers/specs/2026-10-26-release-packages-design.md

## Global constraints

- macOS arm64, Linux x86_64, Windows x86_64; standard hosted runners only.
- No automatic builds for normal pushes or pull requests; manual runs never publish.
- Bundle Qt/QML, MLT plugins/data, ffmpeg/ffprobe, dependent libraries and license notices.
- Preserve existing uncommitted README/setup-guide/screenshots edits.
- Do not publish a real release or require paid signing credentials.

## Review focus

- A clean end-user environment must not fall back to build-machine dependency paths.
- MLT loads plugins dynamically, so executable dependency scans alone are insufficient.
- Release tags and names must never become shell code.
- No release downloads are attached until all three platform packages pass checks.
- Downloaded packages need runtime launch and media checks, not just compilation.

### Task 1: Portable runtime and smoke check

**Files:** src/runtime_paths.h, src/runtime_paths.cpp, src/mlt_engine.cpp, src/main.cpp, tests/runtime_paths_test.cpp, CMakeLists.txt.
**Interfaces:** Bundle layouts: Mac Contents/MacOS + ../PlugIns/mlt-7 + ../Resources/mlt-7; Linux usr/bin + ../lib/mlt-7 + ../share/mlt-7; Windows root + lib/mlt-7 + share/mlt-7. Executable smoke interface: --smoke-test FIXTURE. Returns zero only after QML load, bundled tool checks, media read/export pass.

- [ ] Test missing bundles, each layout, and paths with spaces before implementing runtime path discovery.
- [ ] Implement minimal path discovery; explicitly pass bundled MLT repository to Factory::init.
- [ ] Add package smoke entry point and verify against a real fixture.

### Task 2: Platform packaging

**Files:** packaging/package-macos.py, packaging/package-linux.sh, packaging/package-windows.sh, packaging/README.md.
**Interfaces:** Scripts accept BUILD_DIR OUTPUT_DIR VERSION, create a single versioned platform download, and follow Task 1 layouts. Parent workflow runs package smoke checks. No deployment credentials.

- [ ] Package Mac app with macdeployqt, copy MLT/tool dependencies, rewrite library references, ad-hoc sign; reject external Homebrew references.
- [ ] Package Linux AppDir with linuxdeploy/Qt and MLT/tool dependencies; emit AppImage.
- [ ] Package Windows app with windeployqt, MLT data/plugins, transitive DLL closure; emit ZIP.
- [ ] Include application/dependency notices and fail on unresolved non-system libraries.

### Task 3: Release workflow and documentation

**Files:** .github/workflows/release.yml, docs/releases.md; surgical links in existing documentation.
**Interfaces:** release:published and workflow_dispatch; manual builds upload temporary validation artifacts only. Release assets upload after all jobs succeed.

- [ ] Add dependency installation and native builds on standard runners; package and smoke-test every output.
- [ ] Validate workflow syntax, shell scripts, package contents and local Mac bundle; run relevant existing tests.
- [ ] Review complete change, resolve actionable findings, document how to publish a release and unsigned package limitations.
