# Downloadable releases

The **Release downloads** GitHub Actions workflow builds self-contained downloads when you publish a GitHub Release. Normal commits and pull requests do not start it. Each package includes Qt/QML, MLT and its required plugins, FFmpeg/ffprobe, runtime libraries, and license notices.

| Download | Target |
| --- | --- |
| `IsaacVideoEditor-VERSION-macos-arm64.zip` | Apple silicon Mac |
| `IsaacVideoEditor-VERSION-linux-x86_64.AppImage` | Linux x64 with glibc 2.39 or newer, such as Ubuntu 24.04+ |
| `IsaacVideoEditor-VERSION-windows-x86_64.zip` | Windows x64 |

These builds do not cover Intel Macs, Windows ARM64, or older Linux systems. Packages are not signed with paid distribution certificates or notarized. macOS packages are signed ad hoc for executable integrity; operating-system download security checks can still require approval to open them.

## Publish a version

1. Commit and push the source you want to release, including `.github/workflows/release.yml` and `packaging/`.
2. Open the repository's [Releases page](https://github.com/p-shen/IsaacVideoEditor/releases) and choose **Draft a new release**.
3. Choose or create a version tag such as `v0.1.0` pointing to that commit, add release notes, then publish. Use letters, numbers, dots, underscores, and hyphens in the tag; it must start with a letter or number and contain at most 100 characters.
4. Follow **Release downloads** in the [Actions tab](https://github.com/p-shen/IsaacVideoEditor/actions). The three platform jobs build and package the app, then check the actual downloads by loading the interface, reading and playing a video, and exporting an MP4.
5. Once all three jobs pass, the upload job attaches the packages and `SHA256SUMS.txt` to the release. Until then, GitHub's automatically generated source archives are source code, not runnable app downloads.

Prereleases also build packages. Saving a draft does not start the workflow. If a job fails, check its log, fix the issue, and publish a corrected version; rerunning the original workflow continues to use the original release commit. Rerunning the upload job replaces assets with the same names.

This workflow attaches files after publication and requires release immutability to remain disabled, as it was when configured. If you enable immutable releases, change the process to upload all assets to a draft before publishing it.

## Validate without publishing

In **Actions → Release downloads → Run workflow**, choose the branch to check. A manual run builds and checks all three packages but does not create or change a release. Its downloadable CI artifacts expire after one day.

Successful release runs remove their temporary CI artifacts after attaching the permanent Release downloads. Failed runs retain intermediate artifacts for at most one day. Standard GitHub-hosted runners are used; no larger runners, paid signing services, or personal access tokens are required. The publication job uses GitHub's built-in token with permission to upload release assets and delete its temporary artifacts.

## Run a download

- **macOS:** extract the ZIP and move `IsaacVideoEditor.app` to Applications. The initial package targets Apple silicon.
- **Linux:** make the AppImage executable with `chmod +x IsaacVideoEditor-*-linux-x86_64.AppImage`, then open it. If FUSE is unavailable, run it with `APPIMAGE_EXTRACT_AND_RUN=1`.
- **Windows:** extract the entire ZIP, open its `IsaacVideoEditor` folder, and launch `IsaacVideoEditor.exe`. Keep the included DLLs, plugins, and data folders beside it.

You do not need the development tools listed in [the build guide](building.md) to use these packages. Keep your original media files available when opening saved projects.
