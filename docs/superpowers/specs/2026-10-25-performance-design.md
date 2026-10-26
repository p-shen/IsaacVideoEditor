# Preview preparation and export speed

Implement the three improvements approved in chat: Apple hardware H.264 export with software fallback; prepare only used source ranges, prioritizing the playhead; suspend preparation during export.

Keep the existing C++/Qt/MLT/FFmpeg stack and original project paths, native export dimensions, 60 fps timeline, rotations, fades, audio and safe output replacement. No new UI or dependencies.

## Approach

- Try VideoToolbox H.264 on macOS, using a resolution-dependent bitrate. Rebuild and retry with the existing libx264 settings when hardware rendering fails or produces no video. Validate the temporary output before atomic publication. Other platforms retain software export.
- Keep native-cadence FFmpeg proxies. Add range requests and ready-copy source offsets, reuse completed covering copies (including existing full-source cache files), cancel obsolete jobs, and order queued work around the playhead. Small handles permit trimming near a boundary. Align segment starts to both the source cadence and the 60 fps timeline so fractional-rate footage does not change frames when rebased. Verify all video packet timestamps once per source fingerprint in a cancellable background process; accept only a regular presentation timeline and align to its timestamp clock as well. A 10-second/16-MiB bound falls back to the original time origin. When cadence is unknown or variable, preserve the original start and trim only the unused tail.
- Pause preparation by stopping and requeuing the active disposable partial job; no FFmpeg process remains running during export. Resume after successful or failed export.

Alternatives considered: faster proxy presets trade modest gains for much larger cache files; more rendering threads showed inconsistent gains; forcing proxies to 60 fps changes existing resampling semantics. Retain the existing codecs/cadence for preview.

## Validation

Test trimmed-range duration and offset, every-frame alignment for integer/fractional source rates, audio and camera orientation, covering-cache reuse, changed sources, cancellation, priority and pause/resume. Verify UI substitution and export lifecycle, fallback and destination preservation, output frames/resolution/audio, full CTest, and an actual hardware render outside restricted execution. Review the final changes independently.
