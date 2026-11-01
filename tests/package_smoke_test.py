"""Exercise the real executable's package check, including its failure path."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

executable = Path(sys.argv[1]).resolve()
fixture = Path(__file__).parent / "fixtures/seek.mp4"
environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                   SDL_AUDIODRIVER="dummy", QT_FORCE_STDERR_LOGGING="1")
if os.environ.get("ISAAC_CLEAN_RUNTIME") == "1":
    environment["PATH"] = (os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")
                           if os.name == "nt" else "/usr/bin:/bin")
    for name in ("QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                 "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH"):
        environment.pop(name, None)
def check_package(executable, source, expected_success):
    result = subprocess.run([str(executable), "--smoke-test", str(source.resolve())],
                            env=environment, text=True, capture_output=True, timeout=45)
    if (result.returncode == 0) != expected_success:
        print(f"Smoke process failed: {executable}; source={source}; "
              f"exit={result.returncode} (0x{result.returncode & 0xffffffff:08x})", file=sys.stderr)
        print(result.stdout + result.stderr, file=sys.stderr)
        if os.name == "nt" and os.environ.get("ISAAC_CLEAN_RUNTIME") == "1":
            diagnostic = subprocess.run([str(executable), "--smoke-test", str(source.resolve())],
                                        env=dict(os.environ, QT_QPA_PLATFORM="offscreen",
                                                 QT_QUICK_BACKEND="software", SDL_AUDIODRIVER="dummy",
                                                 QT_FORCE_STDERR_LOGGING="1"),
                                        text=True, capture_output=True, timeout=45)
            print(f"With development PATH: exit={diagnostic.returncode} "
                  f"(0x{diagnostic.returncode & 0xffffffff:08x})\n"
                  + diagnostic.stdout + diagnostic.stderr, file=sys.stderr)
        raise SystemExit(1)
    if expected_success:
        assert "Package smoke test passed" in result.stderr, result.stderr
        assert "playback" in result.stderr, "Package check must exercise the SDL playback backend"


for source, expected_success in [(fixture, True), (fixture.with_name("missing.mp4"), False)]:
    check_package(executable, source, expected_success)

if sys.platform == "darwin" and os.environ.get("ISAAC_CLEAN_RUNTIME") == "1":
    with tempfile.TemporaryDirectory(prefix="isaac-plugin-discovery-") as temporary:
        copied_app = Path(temporary) / "IsaacVideoEditor.app"
        shutil.copytree(executable.parents[2], copied_app, symlinks=True)
        (copied_app / "Contents/Resources/qt.conf").write_text(
            "[Paths]\nPrefix=/isaac-missing-qt-runtime\nPlugins=missing-plugins\nQmlImports=missing-qml\n")
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(copied_app)],
                       check=True, capture_output=True)
        check_package(copied_app / "Contents/MacOS/IsaacVideoEditor", fixture, True)
        print("Bundled Qt plugins and QML work independently of qt.conf discovery.")

print("Package smoke success and missing-media checks passed.")
