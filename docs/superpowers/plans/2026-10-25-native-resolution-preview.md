# Native export resolution and smooth preview

Use each original file's actual native dimensions instead of assuming a 2K/4K label. Select the smallest native raster by pixel count for the export canvas, normalize to landscape to retain the editor's existing orientation behavior, and round to even H.264 dimensions. Other sources fit without cropping or stretching. Keep 60 fps.

The current preview already requests 960×540 frames, but still decodes full 4K HEVC first. A synthetic MLT benchmark measured 25.5 fps for original 4K/60 HEVC versus 527 fps for a 540p H.264 copy. Lowering the profile alone did not remove the bottleneck. Hardware acceleration remains unverified.

- [x] Probe and cache original dimensions/duration, choose native export size, and verify actual mixed-2K/4K and all-4K MP4 exports.
- [x] Add sequential background FFmpeg preview copies, with source-fingerprint caching, atomic publication, exact frame count, audio, orientation, cancellation, and graceful fallback.
- [x] Keep original project paths and export sources intact; substitute copies only in a temporary preview project. Use an appropriately sized preview profile and preserve position/playback when copies become ready.
- [x] Verify temporal alignment, UI integration, profile changes, full tests, and an independent review. Rebuild the app and update usage notes.

Cached previews are H.264/PCM MOV, at most 960×540, preserving the source frame cadence on the 60 fps project timeline. Forcing every copy to 60 fps was found to shift some preview frames relative to MLT's native resampling; retaining native cadence fixes that discrepancy. Initial preparation uses the original files; subsequent editing can use the cached copies. The header shows actual export dimensions. Preview status shows preparation progress. Cache failures fall back to original media.


## Verification

Release build succeeded; all nine CTest targets passed. Actual exported files verify mixed 4K/2K at 2560×1440 and all-4K at 3840×2160, both 60 fps. An offscreen MainWindow workflow exercised drag/drop, cache-ready switching at frame 75, source-path preservation, damaged-cache fallback/discard, and export through the real save dialog. The resulting 133-frame export matched the displayed edit; its reimported MP4 source has a longer AAC container tail, and the existing MLT importer duration semantics were deliberately retained. Original source hashes remained unchanged; QA cache files were removed.

Native-cadence proxy tests compare every frame for 30/29.97/59.94 fps; additional synthetic checks covered 60 fps and VFR. Audio sample counts aligned, with differences within PCM16 quantization. Corrupt cached files fail back to originals on open/initial-decode failure. Later-frame corruption recovery is not added. Synthetic measurements are not a real DJI playback guarantee; hardware acceleration remains unverified.
