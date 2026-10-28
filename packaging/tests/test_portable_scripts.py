"""Portable packaging safety checks; native bundle smoke checks run in CI."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PACKAGING = Path(__file__).resolve().parents[1]


class ScriptInputsTest(unittest.TestCase):
    def test_rejects_unsafe_versions_before_touching_output(self):
        for platform in ("linux", "windows"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                output = root / "keep"
                output.mkdir()
                marker = output / "important"
                marker.write_text("keep me")
                for version in ("../escape", "v1;echo bad", "", "-flag", "v1\nextra"):
                    result = subprocess.run(
                        ["bash", str(PACKAGING / f"package-{platform}.sh"), str(root), str(output), version],
                        capture_output=True, text=True,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("version", result.stderr.lower())
                    self.assertEqual(marker.read_text(), "keep me")
                    self.assertEqual(sorted(p.name for p in output.iterdir()), ["important"])

    def test_requires_all_three_arguments(self):
        for platform in ("linux", "windows"):
            with self.subTest(platform=platform):
                result = subprocess.run(["bash", str(PACKAGING / f"package-{platform}.sh")], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("usage:", result.stderr.lower())


class MltModulesTest(unittest.TestCase):
    def test_copies_required_services_and_excludes_qt_and_optional_modules(self):
        for extension in ("so", "dll"):
            with self.subTest(extension=extension), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                source, target = root / "source", root / "bundle"
                source.mkdir()
                target.mkdir()
                for module in ("core", "avformat", "normalize", "resample", "sdl2", "xml", "qt", "qt6", "frei0r"):
                    (source / f"libmlt{module}.{extension}").write_text(module)
                result = subprocess.run(["bash", str(PACKAGING / "copy-mlt-modules.sh"), str(source), str(target), extension], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(sorted(p.stem for p in target.iterdir()), ["libmltavformat", "libmltcore", "libmltnormalize", "libmltresample", "libmltsdl2", "libmltxml"])

    def test_fails_if_a_required_module_is_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "source").mkdir()
            (root / "bundle").mkdir()
            result = subprocess.run(["bash", str(PACKAGING / "copy-mlt-modules.sh"), str(root / "source"), str(root / "bundle"), "so"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("core", result.stderr)


class LinuxRuntimeHookTest(unittest.TestCase):
    def test_sets_bundle_paths_and_preserves_arguments_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="app dir ") as tmp:
            root = Path(tmp)
            (root / "usr/bin").mkdir(parents=True)
            executable = root / "usr/bin/IsaacVideoEditor"
            executable.write_text('#!/bin/sh\nprintf "%s\\n" "$PATH" "$MLT_REPOSITORY" "$MLT_DATA" "$MLT_PROFILES_PATH" "$LD_LIBRARY_PATH" "$APPDIR" "$@"\n')
            executable.chmod(0o755)
            result = subprocess.run(["bash", "-c", 'this_dir=$1; source "$2"; shift 2; exec "$this_dir/usr/bin/IsaacVideoEditor" "$@"', "launcher", str(root), str(PACKAGING / "linux-runtime-hook.sh"), "clip one.mp4", "--test"], capture_output=True, text=True, check=True)
            lines = result.stdout.splitlines()
            self.assertTrue(lines[0].startswith(f"{root}/usr/bin:"))
            self.assertEqual(lines[1:4], [f"{root}/usr/lib/mlt-7", f"{root}/usr/share/mlt-7", f"{root}/usr/share/mlt-7/profiles"])
            self.assertTrue(lines[4].startswith(f"{root}/usr/lib"))
            self.assertEqual(lines[5], str(root))
            self.assertEqual(lines[6:], ["clip one.mp4", "--test"])


class QtDeploymentInputsTest(unittest.TestCase):
    def deploy_options(self, platform):
        with tempfile.TemporaryDirectory(prefix="qt deploy ") as tmp:
            root = Path(tmp)
            tools, prefix, build = root / "tools", root / "ucrt64", root / "build"
            modules, data = prefix / "lib/mlt", prefix / "share/mlt"
            for path in (tools, prefix / "bin", build, modules, data / "profiles"):
                path.mkdir(parents=True)
            def tool(name, body):
                path = tools / name
                path.write_text("#!/bin/sh\n" + body)
                path.chmod(0o755)
                return path
            for name in ("ffmpeg", "ffprobe", "objdump", "pacman", "dpkg-query", "ldd", "readelf"):
                tool(name, "exit 0\n")
            tool("uname", 'if [ "$1" = -s ]; then echo Linux; else echo x86_64; fi\n')
            tool("qmake", 'echo "$TEST_PREFIX"\n')
            tool("pkg-config", 'case "$1" in --variable=moduledir) echo "$TEST_PREFIX/lib/mlt";; --variable=mltdatadir) echo "$TEST_PREFIX/share/mlt";; --variable=libdir) echo "$TEST_PREFIX/lib";; *) echo "$TEST_PREFIX";; esac\n')
            extension = "so" if platform == "linux" else "dll"
            for module in ("core", "avformat", "normalize", "resample", "sdl2", "xml"):
                (modules / f"libmlt{module}.{extension}").write_text(module)
            binary = build / ("IsaacVideoEditor" if platform == "linux" else "IsaacVideoEditor.exe")
            binary.write_text("fixture")
            binary.chmod(0o755)
            helper = build / ("IsaacVideoEditorUpdater" if platform == "linux" else "IsaacVideoEditorUpdater.exe")
            helper.write_text("fixture")
            helper.chmod(0o755)
            for name in ("ffmpeg.exe", "ffprobe.exe"):
                (prefix / "bin" / name).write_text("fixture")
            capture = root / "deployment.json"
            deployer = tools / ("linuxdeploy" if platform == "linux" else "windeployqt")
            deployer.write_text(f"#!{sys.executable}\nimport json, os, pathlib, sys\npathlib.Path(os.environ['DEPLOY_CAPTURE']).write_text(json.dumps({{'args': sys.argv[1:], 'platforms': os.environ.get('EXTRA_PLATFORM_PLUGINS'), 'modules': os.environ.get('EXTRA_QT_MODULES')}}))\nsys.exit(77)\n")
            deployer.chmod(0o755)
            env = {**os.environ, "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                   "TEST_PREFIX": str(prefix), "MINGW_PREFIX": str(prefix), "MSYSTEM": "UCRT64",
                   "LINUXDEPLOY": str(deployer), "QMAKE": str(tools / "qmake"), "DEPLOY_CAPTURE": str(capture),
                   "EXTRA_PLATFORM_PLUGINS": "libqwayland.so", "EXTRA_QT_MODULES": "network"}
            result = subprocess.run(["bash", str(PACKAGING / f"package-{platform}.sh"), str(build), str(root / "out"), "test"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 77, result.stderr)
            return json.loads(capture.read_text())

    def test_linux_requests_offscreen_and_svg_preserving_existing_extras(self):
        options = self.deploy_options("linux")
        self.assertEqual(options["platforms"], "libqwayland.so;libqoffscreen.so")
        self.assertEqual(options["modules"], "network;svg")

    def test_windows_requests_offscreen_and_svg_plugins(self):
        args = self.deploy_options("windows")["args"]
        self.assertIn("--svg", args)
        self.assertEqual(set(args[args.index("--include-plugins") + 1].split(",")), {"qoffscreen", "qsvg"})


class WindowsClosureTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dll closure ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "package"
        self.bin = self.root / "ucrt64/bin"
        self.system = self.root / "Windows/System32"
        for path in (self.stage / "lib/mlt-7", self.bin, self.system):
            path.mkdir(parents=True)
        # Real objdump cannot inspect PE binaries on every host running these tests.
        self.objdump = self.root / "objdump"
        self.objdump.write_text(f'#!{sys.executable}\nimport pathlib, sys\nprint("file format pei-x86-64")\nprint(pathlib.Path(sys.argv[-1]).read_text())\n')
        self.objdump.chmod(0o755)

    def binary(self, path, *imports):
        path.write_text("\n".join(f"DLL Name: {name}" for name in imports))

    def collect(self):
        return subprocess.run(
            [sys.executable, str(PACKAGING / "windows-dll-closure.py"), str(self.stage), str(self.bin.parent), str(self.objdump)],
            env={**os.environ, "SystemRoot": str(self.system.parent)}, capture_output=True, text=True,
        )

    def test_copies_transitive_imports_and_dynamic_plugins_without_unrelated_dlls(self):
        self.binary(self.stage / "IsaacVideoEditor.exe", "Qt6Core.DLL", "KERNEL32.dll", "api-ms-win-core-test.dll")
        self.binary(self.stage / "lib/mlt-7/mltcore.dll", "libmlt-7.dll")
        self.binary(self.bin / "Qt6Core.dll", "libgcc.dll")
        self.binary(self.bin / "libgcc.dll", "Qt6Core.dll")
        self.binary(self.bin / "libmlt-7.dll", "libgcc.dll")
        self.binary(self.bin / "unrelated.dll")
        self.binary(self.system / "kernel32.dll")
        result = self.collect()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in self.stage.glob("*.dll")), ["Qt6Core.dll", "libgcc.dll", "libmlt-7.dll"])

    def test_fails_on_missing_transitive_import(self):
        self.binary(self.stage / "IsaacVideoEditor.exe", "first.dll")
        self.binary(self.bin / "first.dll", "not-shipped.dll")
        result = self.collect()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not-shipped.dll", result.stderr)

    def test_refuses_msys_runtime(self):
        self.binary(self.stage / "IsaacVideoEditor.exe", "msys-2.0.dll")
        self.binary(self.bin / "msys-2.0.dll")
        result = self.collect()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("msys-2.0.dll", result.stderr)


if __name__ == "__main__":
    unittest.main()
