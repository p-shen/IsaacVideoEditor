# Recent music downloads

**Goal:** Add a downloaded music track without browsing for the file manually.

**Approach:** Keep Browse Mixkit and Choose. Add a native Recent downloads menu in Audio → Background music. Read the system Downloads folder whenever the menu opens, show the ten newest completed audio files, and import the chosen file through the existing validation and Undo flow. Reference the local source as existing music imports do; project format and playback stay unchanged.

The user selected recent downloads after finding that Mixkit does not expose a convenient track link. The earlier paste-link idea is superseded; no downloader is needed.

- [x] Add a failing file-discovery test covering recency, refresh, uppercase extensions, unsupported files, directories, empty/partial downloads, limits, and missing folders.
- [x] Implement `Media::recentAudioFiles(directory, limit = 10)` using Qt's directory listing, without new dependencies.
- [x] Add the Recent downloads menu, an informative empty state and Choose fallback, and share the music import path.
- [x] Update the download hint and README.
- [x] Build, run all tests, and review the change. Preserve the running editor's unsaved project.

Review focus: Downloads may be empty or unavailable; a file may disappear after the menu opens; filenames may contain ampersands or spaces; browser partial files must not appear; reopening the menu must find newly finished downloads.

Workspace: this folder has no Git repository, so changes are made in place. Verification artifacts and before-copies live in `/tmp`, outside the source tree.

Verification: the focused discovery test failed with the empty implementation, then passed with the directory scan. Release build succeeded and all nine CTest targets passed. Independent read-only review found no confirmed Critical or Important issues. The unrelated update making file dialogs start in Movies was preserved. The running app was not restarted; live menu appearance, VoiceOver, and actual browser downloads remain unchecked.
