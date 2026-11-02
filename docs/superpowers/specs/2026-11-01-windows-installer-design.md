# Windows installer

## Intent

Provide a single setup download so people launch IsaacVideoEditor from Start or a desktop shortcut without browsing its dependency files.

## Approach

Use Inno Setup, as selected in the conversation. The portable ZIP remains available and continues to supply in-app updates. MSIX would add a different installation and update model; a ZIP alone retains the current launch problem.

## Installation

- Produce `IsaacVideoEditor-VERSION-windows-x86_64-Setup.exe` from the same staged application files as the portable ZIP.
- Install for the current user under `%LOCALAPPDATA%\Programs\IsaacVideoEditor`, without requesting administrator access.
- Create a Start menu shortcut, offer an optional desktop shortcut, and register an uninstaller in Windows Settings.
- Use a stable application identity and installation path so later setup versions upgrade the existing installation.
- Keep the existing portable marker and file manifest so in-app updates still work. Installer-created files, including the uninstaller, must survive those updates.
- Uninstallation removes installed application files and shortcuts while preserving projects, source media, and settings. Avoid broad directory deletion.

## Release integration

Extend the existing Windows packaging job to compile and upload both setup and portable downloads. Include the installer in release checksums. Keep the updater selecting the portable ZIP, not the setup executable. Document the setup download as the normal Windows installation path.

## Scope and verification

Reuse the current application payload and updater; no editor behavior, project format, paid signing, or new release version is required. Validate installer compilation and the installed application's launch on Windows, shortcut creation, upgrade behavior, uninstallation, and preservation of installer files during an in-app update. Focus checks on packaging rather than rerunning the full editor suite.
