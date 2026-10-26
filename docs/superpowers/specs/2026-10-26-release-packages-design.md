# Release packages design

Publishing a GitHub Release builds downloadable IsaacVideoEditor packages for macOS arm64, Linux x86_64, and Windows x86_64 using standard hosted runners. Normal pushes and pull requests do not build packages. A manual validation run may build without publishing a release.

Each package includes Qt/QML, MLT plugins/data, FFmpeg/ffprobe, dependent libraries, and license notices. The application locates bundled runtime files relative to its executable; ordinary development builds retain system dependency discovery. A smoke check loads the QML interface, reads a media fixture, and exports media using the packaged dependencies before uploads.

Use a macOS ZIP containing an ad-hoc-signed app, a Linux AppImage, and a Windows portable ZIP. Paid certificates, installers, Intel Mac/ARM Windows builds, and creating a real release are outside this change. Release assets are uploaded together only after every platform passes. Existing documentation edits are preserved.

Approaches: compiling bare executables does not satisfy download-and-run; fully managed installer/signing services add cost and credentials. Native Qt deployment tools plus small platform packaging scripts keep the existing CMake/dependency setup and standard free public runners. Publishing-on-release is the approach approved in the conversation; manual validation exists solely to verify packaging before a real release.
