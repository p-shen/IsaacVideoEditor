# In-app updates

The requested change replaces the GitHub browser handoff with Download Update, progress/cancel, and Restart and Install in the existing Help dialog. Keep the existing latest-stable-release check and numeric tag comparison.

Use the existing macOS arm64 ZIP, Windows x64 portable ZIP, and Linux x64 AppImage. Match the asset's exact repository filename, require GitHub's SHA-256 digest and advertised size, stream over HTTPS, and reject damaged or incomplete downloads before preparing an install. ZIPs use the operating system's libarchive tar with safe entry validation, followed by validation of the extracted payload; macOS also verifies the bundle signature. Unsupported source/architecture layouts and unwritable install folders produce an in-app explanation.

An independent native helper has no Qt dependencies. It copies the verified payload to a sibling staging directory, acknowledges readiness, waits for the editor to exit, and replaces the application through same-volume renames. It restores the old application if replacement or launching fails. Save/discard/cancel goes through the editor's existing close handling before helper launch. Original media, project files, and application settings are never part of replacement. No administrator escalation or silent background installation.

The existing package approach avoids a new updater framework, signing-key service, or release-feed system. Platform frameworks were considered, but would require new release metadata/signing configuration and separate integration on each supported OS. Downloads stay within the app and no browser handoff remains.

Verify metadata/URL selection, streaming size and checksum, cancellation, malformed archives, missing payloads, helper waiting/atomic replacement/rollback, paths with spaces, save cancellation, and existing editing regressions. Use disposable installation fixtures for replacement tests. Native Windows/Linux execution requires those platforms.
