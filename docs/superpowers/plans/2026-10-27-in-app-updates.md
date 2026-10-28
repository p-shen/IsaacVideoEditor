# In-app updates implementation

User request: download and install releases directly in the app. Design: ../specs/2026-10-27-in-app-updates-design.md.

- [ ] Add native dependency-free `IsaacVideoEditorUpdater` with temporary-install tests, parent-exit acknowledgement, atomic replacement, rollback, and native relaunch.
- [ ] Add exact platform asset selection and bounded streaming download with SHA-256/size checks, cancellation, safe archive preparation, and install handoff to `UpdateChecker`.
- [ ] Replace View Release with Download Update / Cancel Download / Restart and Install; reuse normal save protection and close only after helper readiness.
- [ ] Bundle the helper on all platforms, identify Windows portable installations, document requirements, and exercise package fixtures.
- [ ] Run focused updater/installer/UI tests, inspect light/dark/large-text layouts, run the application build and regression suite, and request an independent review.

Preserve the existing staged work and index. All destructive installation tests use temporary application copies. Do not install an older published release over the user's working application to test the feature.
