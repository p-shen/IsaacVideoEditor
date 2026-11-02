# Windows Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver an Inno Setup download with normal Windows shortcuts and uninstallation.
**Architecture:** Compile the installer from the same staged payload as the ZIP. Retain the ZIP updater and its manifest-based preservation of installer-owned files.
**Tech Stack:** Inno Setup 6, Python standard library, MSYS2, GitHub Actions.
**Spec:** `docs/superpowers/specs/2026-11-01-windows-installer-design.md`

## Global Constraints

- Per-user installation at `%LOCALAPPDATA%\Programs\IsaacVideoEditor`; no elevation.
- Stable identity, Start shortcut, optional desktop shortcut, portable ZIP retained.
- Preserve media, projects, settings and uninstaller across updates. No broad directory deletion.

## Review Focus

- Compiler paths containing spaces must be passed as individual arguments.
- Invalid version strings and existing output files must fail before compilation.
- The setup EXE must not replace the ZIP selected by the updater.
- Reinstalling must retain one installation and preserve user-added files.
- Uninstaller metadata must remain outside the portable manifest and survive updates.

### Task 1: Installer and release integration

**Files:** Create `packaging/windows-installer.iss`, `packaging/package-windows-installer.py`, `packaging/tests/test_windows_installer.py`, and `tests/windows_installer_smoke_test.py`. Modify `packaging/package-windows.sh`, `.github/workflows/release.yml`, `packaging/README.md`, and `docs/releases.md`.

**Interfaces:** Builder consumes staged payload directory, output directory, artifact label and application version. Produces `IsaacVideoEditor-VERSION-windows-x86_64-Setup.exe` beside the existing ZIP.

- [ ] Add focused builder tests using a fake compiler: version validation, output collision, missing payload, arguments containing spaces, and compiler failure.
- [ ] Observe failing tests before adding the builder and Inno script; implement the per-user installer with stable identity and shortcuts.
- [ ] Integrate compilation into Windows packaging and include setup files in artifact upload, checksums and release upload. Preserve ZIP selection in the updater.
- [ ] Add Windows-only install/reinstall/shortcut/launch/uninstall checks on disposable CI runners. Preserve a user-added project during reinstall and uninstall; verify installer metadata is absent from the update manifest.
- [ ] Run focused Python packaging tests, then dispatch Windows validation on the implementation branch. Expected: compiled installer and successful native lifecycle checks.
- [ ] Obtain a fresh review, address material findings, and commit the implementation. Record any unverified limits explicitly.
