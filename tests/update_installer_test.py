#!/usr/bin/env python3
"""Exercise the standalone updater against disposable installations."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


HELPER_ARGUMENT = sys.argv.pop(1) if len(sys.argv) > 1 and Path(sys.argv[1]).is_absolute() else None
ACK_SCRIPT = '''if [ "$1" = "--update-started" ]; then
    printf 'IsaacVideoEditor started 1\\n' > "$2"
    shift 2
fi
'''


@unittest.skipIf(os.name == "nt", "The shell fixtures run on macOS and Linux")
class UpdateInstallerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="isaac updater tests ")
        supplied = HELPER_ARGUMENT or os.environ.get("ISAAC_UPDATE_HELPER")
        cls.helper = Path(cls.build.name).resolve() / "IsaacVideoEditorUpdater"
        source = Path(__file__).resolve().parents[1] / "src" / "update_installer.cpp"
        if supplied:
            shutil.copy2(Path(supplied).resolve(), cls.helper)
        elif source.exists():
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(cls.helper)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.assertTrue(self.helper.is_file(), "The standalone update installer has not been implemented")
        self.folder = tempfile.TemporaryDirectory(prefix="isaac installation with spaces ")
        self.root = Path(self.folder.name).resolve()
        self.prepared = self.root / "prepared update"
        self.prepared.mkdir(mode=0o700)
        self.directory_payload = sys.platform == "darwin"
        self.target = self.root / ("Isaac Video Editor.app" if self.directory_payload else "Isaac Video Editor.AppImage")
        self.payload = self.prepared / "payload"
        self.relative = "Contents/MacOS/IsaacVideoEditor" if self.directory_payload else "."
        self.log = self.root / "launch log"
        self.make_app(self.target, "old")
        self.make_app(self.payload, "new")
        (self.prepared / "ready").write_bytes(b"IsaacVideoEditor update 1\n")
        self.parent = subprocess.Popen(["/bin/sleep", "120"])
        self.updater = None
        self.media = self.root / "user video.mov"
        self.media.write_text("keep this media")

    def tearDown(self):
        if self.updater is not None and self.updater.poll() is None:
            self.updater.terminate()
            self.updater.wait(timeout=5)
        if self.parent.poll() is None:
            self.parent.terminate()
        self.parent.wait(timeout=5)
        self.folder.cleanup()

    def executable(self, app):
        return app / self.relative if self.directory_payload else app

    def make_app(self, app, version):
        executable = self.executable(app)
        executable.parent.mkdir(parents=True, exist_ok=True)
        executable.write_text("#!/bin/sh\n" + ACK_SCRIPT + "printf '%s\\n' " + shlex.quote(version) + " \"$@\" >> " + shlex.quote(str(self.log)) + "\n")
        executable.chmod(0o755)

    def start(self, prepared=None, target=None, relative=None, pid=None, extra_env=None):
        env = os.environ.copy()
        env.update(extra_env or {})
        self.updater = subprocess.Popen([str(self.helper), "--install", str(pid or self.parent.pid),
                                         str(prepared or self.prepared), str(target or self.target),
                                         relative or self.relative], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def wait_for(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.02)
        self.fail("Timed out waiting for installer state: " + self.diagnostics())

    def arm(self):
        self.start()
        self.wait_for(lambda: (self.prepared / "armed").exists() or self.updater.poll() is not None)
        self.assertTrue((self.prepared / "armed").is_file(), self.diagnostics())

    def diagnostics(self):
        error = self.prepared / "error.txt"
        return error.read_text() if error.exists() else "No error diagnostic"

    def stop_parent(self):
        self.parent.terminate()
        self.parent.wait(timeout=5)

    def finish(self):
        output = self.updater.communicate(timeout=10)
        return self.updater.returncode, output

    def assert_original(self):
        self.assertIn("old", self.executable(self.target).read_text())
        self.assertEqual(self.media.read_text(), "keep this media")

    def test_waits_until_parent_exits_then_swaps_and_relaunches(self):
        self.arm()
        time.sleep(0.2)
        self.assertIsNone(self.updater.poll())
        self.assert_original()
        self.assertFalse(self.log.exists())
        self.stop_parent()
        self.assertEqual(self.finish()[0], 0, self.diagnostics())
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["new"])
        self.assertIn("new", self.executable(self.target).read_text())
        self.assertFalse(self.prepared.exists())
        self.assertFalse(list(self.root.glob(".isaac-update-*")))
        self.assertEqual(self.media.read_text(), "keep this media")

    def test_bad_ready_marker_leaves_original_untouched(self):
        (self.prepared / "ready").write_text("not an Isaac update\n")
        self.start()
        self.assertNotEqual(self.finish()[0], 0)
        self.assertIn("ready", self.diagnostics().lower())
        self.assertFalse((self.prepared / "armed").exists())
        self.assert_original()

    def test_relative_target_is_rejected(self):
        self.start(target=Path("relative.app"))
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_dot_dot_path_is_rejected(self):
        self.start(target=self.root / "prepared update" / ".." / self.target.name)
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_wrong_relative_executable_is_rejected(self):
        self.start(relative="../IsaacVideoEditor")
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_invalid_parent_pid_is_rejected(self):
        self.start(pid=1)
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_prepared_directory_symlink_is_rejected(self):
        alias = self.root / "prepared alias"
        alias.symlink_to(self.prepared, target_is_directory=True)
        self.start(prepared=alias)
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_target_symlink_is_rejected(self):
        alias = self.root / ("Alias.app" if self.directory_payload else "Alias.AppImage")
        alias.symlink_to(self.target, target_is_directory=self.directory_payload)
        self.start(target=alias)
        self.assertNotEqual(self.finish()[0], 0)
        self.assertTrue(alias.is_symlink())
        self.assert_original()

    def test_payload_symlink_is_rejected(self):
        if self.directory_payload:
            shutil.rmtree(self.payload)
        else:
            self.payload.unlink()
        self.payload.symlink_to(self.target, target_is_directory=self.directory_payload)
        self.start()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    def test_payload_executable_hardlink_to_current_app_is_rejected(self):
        self.executable(self.payload).unlink()
        os.link(self.executable(self.target), self.executable(self.payload))
        self.start()
        self.wait_for(lambda: (self.prepared / "armed").exists() or self.updater.poll() is not None)
        self.assertFalse((self.prepared / "armed").exists(), "A payload must not alias the installed executable")
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    @unittest.skipUnless(sys.platform == "darwin", "Bundle symlinks are specific to directory payloads")
    def test_escaping_bundle_symlink_is_rejected(self):
        (self.payload / "outside").symlink_to(self.root)
        self.start()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()

    @unittest.skipUnless(sys.platform == "darwin", "Bundle symlinks are specific to directory payloads")
    def test_internal_framework_symlink_is_preserved(self):
        versions = self.payload / "Contents/Frameworks/Test.framework/Versions"
        (versions / "A").mkdir(parents=True)
        (versions / "A/Test").write_text("framework")
        (versions / "Current").symlink_to("A", target_is_directory=True)
        self.arm()
        self.stop_parent()
        self.assertEqual(self.finish()[0], 0, self.diagnostics())
        link = self.target / "Contents/Frameworks/Test.framework/Versions/Current"
        self.assertTrue(link.is_symlink())
        self.assertEqual(os.readlink(link), "A")

    def test_missing_staged_executable_cancels_before_replacement(self):
        self.arm()
        stage, = list(self.root.glob(".isaac-update-*"))
        self.executable(stage / "payload").unlink()
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.assertTrue((self.prepared / "error.txt").is_file())
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])

    def test_replaced_installation_is_not_overwritten_while_waiting(self):
        self.arm()
        moved = self.root / "original installation"
        self.target.rename(moved)
        self.make_app(self.target, "user replacement")
        # Replacing a directory can preserve its executable's timestamp.
        old_time = self.executable(moved).stat()
        os.utime(self.executable(self.target), ns=(old_time.st_atime_ns, old_time.st_mtime_ns))
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assertIn("user replacement", self.executable(self.target).read_text())
        self.assertIn("old", self.executable(moved).read_text())

    def test_staging_directory_replacement_is_not_deleted(self):
        self.arm()
        stage, = list(self.root.glob(".isaac-update-*"))
        stage.rename(self.root / "actual staging directory")
        stage.mkdir()
        personal_file = stage / "personal file"
        personal_file.write_text("keep")
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.assertEqual(personal_file.read_text(), "keep")

    def test_exec_failure_restores_and_relaunches_original(self):
        self.executable(self.payload).write_text("#!/nonexistent/isaac-interpreter\n")
        self.arm()
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])
        self.assertIn("launch", self.diagnostics().lower())

    def test_relaunch_clears_old_runtime_environment(self):
        names = ["APPIMAGE", "APPDIR", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM",
                 "QML2_IMPORT_PATH", "MLT_REPOSITORY", "MLT_MOVIT_PATH", "DYLD_FALLBACK_LIBRARY_PATH"]
        self.executable(self.payload).write_text("#!/bin/sh\n" + ACK_SCRIPT + "printf '%s\\n' " +
                                                " ".join('"${' + name + '-unset}"' for name in names) +
                                                " \"${APPIMAGE_EXTRACT_AND_RUN-unset}\" > " + shlex.quote(str(self.log)) + "\n")
        environment = {name: "/nonexistent/old/runtime" for name in names}
        environment["APPIMAGE_EXTRACT_AND_RUN"] = "1"
        self.start(extra_env=environment)
        self.wait_for(lambda: (self.prepared / "armed").exists() or self.updater.poll() is not None)
        self.assertTrue((self.prepared / "armed").exists(), self.diagnostics())
        self.stop_parent()
        self.assertEqual(self.finish()[0], 0, self.diagnostics())
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["unset"] * len(names) + ["1"])

    def test_stale_startup_acknowledgement_is_rejected(self):
        self.arm()
        (self.prepared / "started").write_text("IsaacVideoEditor started 1\n")
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])

    def test_startup_exit_restores_and_relaunches_original(self):
        self.executable(self.payload).write_text("#!/bin/sh\nexit 78\n")
        self.arm()
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])

    def test_invalid_startup_acknowledgement_rolls_back(self):
        self.executable(self.payload).write_text("#!/bin/sh\nprintf 'wrong marker\\n' > \"$2\"\nexit 78\n")
        self.arm()
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])

    def test_startup_acknowledgement_is_required_before_removing_backup(self):
        self.executable(self.payload).write_text("#!/bin/sh\nprintf '%s\\n' \"$2\" > " +
                                                shlex.quote(str(self.log)) + "\nsleep 2\n" + ACK_SCRIPT)
        self.arm()
        self.stop_parent()
        self.wait_for(self.log.exists)
        acknowledgement = Path(self.log.read_text().strip())
        self.assertEqual(acknowledgement, self.prepared / "started")
        stage, = list(self.root.glob(".isaac-update-*"))
        self.assertTrue((stage / "previous").exists())
        self.assertIsNone(self.updater.poll())
        self.assertEqual(self.finish()[0], 0, self.diagnostics())
        self.assertFalse(self.prepared.exists())

    def test_staging_directory_replaced_during_startup_is_not_cleaned_up(self):
        self.executable(self.payload).write_text("#!/bin/sh\nprintf '%s\\n' \"$2\" > " +
                                                shlex.quote(str(self.log)) + "\nsleep 2\n" + ACK_SCRIPT)
        self.arm()
        self.stop_parent()
        self.wait_for(self.log.exists)
        stage, = list(self.root.glob(".isaac-update-*"))
        stage.rename(self.root / "original staging directory")
        stage.mkdir()
        personal_file = stage / "personal file"
        personal_file.write_text("keep")
        self.assertEqual(self.finish()[0], 0, self.diagnostics())
        self.assertTrue(personal_file.exists(), "Success cleanup removed an unrelated replacement directory")
        self.assertEqual(personal_file.read_text(), "keep")

    def rename_failure_environment(self, obstruction=False):
        source = self.root / "fail replacement rename.c"
        source.write_text(r'''
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
static int obstruct_replacement(const char *from, const char *to) {
    const char *base = strrchr(from, '/');
    if (strstr(from, "/.isaac-update-") && base && strcmp(base, "/payload") == 0) {
        if (getenv("ISAAC_TEST_CREATE_OBSTRUCTION")) {
#ifdef __APPLE__
            if (mkdir(to, 0700) != 0) return -1;
#else
            int file = open(to, O_CREAT | O_EXCL | O_WRONLY, 0600);
            if (file < 0) return -1;
            write(file, "user media", 10);
            close(file);
#endif
        } else {
            errno = EIO;
            return -1;
        }
    }
    return 0;
}
#ifdef __APPLE__
static int fail_replacement(const char *from, const char *to, unsigned flags) {
    if (obstruct_replacement(from, to) != 0) return -1;
    return syscall(SYS_renameatx_np, AT_FDCWD, from, AT_FDCWD, to, flags);
}
__attribute__((used)) static struct { const void *replacement, *original; } hooks
__attribute__((section("__DATA,__interpose"))) = { (void *)fail_replacement, (void *)renamex_np };
#else
int renameat2(int from_fd, const char *from, int to_fd, const char *to, unsigned flags) {
    if (obstruct_replacement(from, to) != 0) return -1;
    return syscall(SYS_renameat2, from_fd, from, to_fd, to, flags);
}
#endif
''')
        library = self.root / ("fail rename.dylib" if sys.platform == "darwin" else "fail rename.so")
        flags = ["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC", "-ldl"]
        subprocess.run(["cc", str(source), "-o", str(library)] + flags, check=True, capture_output=True)
        env_name = "DYLD_INSERT_LIBRARIES" if sys.platform == "darwin" else "LD_PRELOAD"
        environment = {env_name: str(library)}
        if obstruction:
            environment["ISAAC_TEST_CREATE_OBSTRUCTION"] = "1"
        return environment

    def test_failed_replacement_rename_restores_original(self):
        self.start(extra_env=self.rename_failure_environment())
        self.wait_for(lambda: (self.prepared / "armed").exists() or self.updater.poll() is not None)
        self.assertTrue((self.prepared / "armed").exists(), self.diagnostics())
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        self.assert_original()
        self.wait_for(self.log.exists)
        self.assertEqual(self.log.read_text().splitlines(), ["old", "--update-failed"])

    def test_newly_occupied_target_is_preserved_with_original_backup(self):
        self.start(extra_env=self.rename_failure_environment(obstruction=True))
        self.wait_for(lambda: (self.prepared / "armed").exists() or self.updater.poll() is not None)
        self.assertTrue((self.prepared / "armed").exists(), self.diagnostics())
        self.stop_parent()
        self.assertNotEqual(self.finish()[0], 0)
        if self.directory_payload:
            self.assertEqual(list(self.target.iterdir()), [])
        else:
            self.assertEqual(self.target.read_text(), "user media")
        stage, = list(self.root.glob(".isaac-update-*"))
        self.assertIn("old", self.executable(stage / "previous").read_text())
        self.assertIn("backup", self.diagnostics().lower())

    @unittest.skipUnless(os.environ.get("ISAAC_UPDATE_TEST_TIMEOUT") == "1", "Set ISAAC_UPDATE_TEST_TIMEOUT=1 to exercise the 90-second timeout")
    def test_parent_timeout_leaves_installation_untouched(self):
        self.arm()
        self.updater.communicate(timeout=95)
        self.assertNotEqual(self.updater.returncode, 0)
        self.assert_original()
        self.assertFalse(self.log.exists())
        self.assertIn("timed out", self.diagnostics().lower())

    @unittest.skipUnless(os.environ.get("ISAAC_UPDATE_TEST_TIMEOUT") == "1", "Set ISAAC_UPDATE_TEST_TIMEOUT=1 to exercise the 30-second startup timeout")
    def test_live_startup_timeout_kills_new_process_and_restores_original(self):
        self.executable(self.payload).write_text("#!/bin/sh\nprintf '%s\\n' \"$$\" > " +
                                                shlex.quote(str(self.log)) + "\nexec /bin/sleep 120\n")
        self.arm()
        self.stop_parent()
        self.wait_for(self.log.exists)
        new_pid = int(self.log.read_text().strip())
        self.updater.communicate(timeout=40)
        self.assertNotEqual(self.updater.returncode, 0)
        self.assert_original()
        self.wait_for(lambda: "--update-failed" in self.log.read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(new_pid, 0)


if __name__ == "__main__":
    unittest.main()
