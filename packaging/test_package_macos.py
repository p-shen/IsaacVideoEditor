"""Run with: python3 -m unittest discover -s packaging -p 'test_package_macos.py'."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name('package-macos.py')


def packager():
    spec = importlib.util.spec_from_file_location('package_macos', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ArgumentsTest(unittest.TestCase):
    def test_unsafe_version_fails_without_touching_caller_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            keep = Path(tmp) / 'keep.txt'
            keep.write_text('keep me')
            result = subprocess.run([sys.executable, str(SCRIPT), tmp, tmp, '../bad'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('VERSION', result.stderr)
            self.assertEqual(keep.read_text(), 'keep me')


@unittest.skipUnless(sys.platform == 'darwin' and shutil.which('clang'), 'requires macOS developer tools')
class RelocationTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.external = self.root / 'external libs'
        self.external.mkdir()
        self.app = self.root / 'IsaacVideoEditor.app'
        self.executable = self.app / 'Contents/MacOS/IsaacVideoEditor'
        self.executable.parent.mkdir(parents=True)
        self.module = packager()

    def compile(self, name, code, *options):
        source = self.root / (name + '.c')
        source.write_text(code)
        subprocess.run(['clang', str(source), '-o', str(self.external / name), *map(str, options)],
                       check=True, capture_output=True)
        return self.external / name

    def make_chain(self):
        leaf = self.compile('libleaf.dylib', 'int leaf(void) { return 42; }',
                            '-dynamiclib', '-Wl,-install_name,@rpath/libleaf.dylib')
        middle = self.compile('libmiddle.dylib', 'extern int leaf(void); int middle(void) { return leaf(); }',
                              '-dynamiclib', '-Wl,-install_name,' + str(self.external / 'libmiddle.dylib'),
                              leaf, '-Wl,-rpath,' + str(self.external))
        executable = self.compile('main', '#include <stdio.h>\nextern int middle(void); int main(void) { printf("%d\\n", middle()); }', middle)
        shutil.copy2(executable, self.executable)
        return leaf, middle

    def test_transitive_rpath_closure_runs_after_sources_are_removed_and_app_is_moved(self):
        self.make_chain()
        self.module.Relocator(self.app).relocate()
        self.module.verify_bundle(self.app)
        moved = self.root / 'folder with spaces' / self.app.name
        moved.parent.mkdir()
        shutil.move(self.app, moved)
        shutil.rmtree(self.external)
        result = subprocess.run([str(moved / 'Contents/MacOS/IsaacVideoEditor')],
                                env={'PATH': '/usr/bin:/bin'}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), '42')
        self.assertTrue((moved / 'Contents/Frameworks/libleaf.dylib').is_file())
        self.module.verify_bundle(moved)

    def test_missing_transitive_library_is_reported(self):
        leaf, _ = self.make_chain()
        leaf.unlink()
        with self.assertRaisesRegex(self.module.PackageError, 'libleaf'):
            self.module.Relocator(self.app).relocate()

    def test_verifier_rejects_external_and_missing_relative_references(self):
        _, middle = self.make_chain()
        with self.assertRaisesRegex(self.module.PackageError, 'external|External'):
            self.module.verify_bundle(self.app)
        subprocess.run(['install_name_tool', '-change', str(middle),
                        '@loader_path/../Frameworks/absent.dylib', str(self.executable)], check=True)
        with self.assertRaisesRegex(self.module.PackageError, 'absent'):
            self.module.verify_bundle(self.app)

    def test_sdl2_compat_audio_uses_bundled_dynamically_loaded_sdl3(self):
        if not shutil.which('brew'):
            self.skipTest('requires Homebrew sdl2-compat')
        result = subprocess.run(['brew', '--prefix', 'sdl2-compat'], capture_output=True, text=True)
        if result.returncode:
            self.skipTest('requires Homebrew sdl2-compat')
        sdl2 = Path(result.stdout.strip()) / 'lib/libSDL2.dylib'
        source = self.compile('audio', 'extern int SDL_Init(unsigned); extern void SDL_Quit(void); int main(void) { int rc = SDL_Init(0x10); SDL_Quit(); return rc; }', sdl2)
        shutil.copy2(source, self.executable)
        sources = {}
        self.module.add_sdl_compat_runtime(self.app, source, sources)
        self.module.Relocator(self.app, sources).relocate()
        self.assertTrue((self.app / 'Contents/Frameworks/libSDL3.dylib').is_file())
        self.module.prepare_for_signing(self.app)
        for binary in self.module.macho_files(self.app):
            subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True, capture_output=True)
        result = subprocess.run([str(self.executable)], env={'PATH': '/usr/bin:/bin', 'SDL_AUDIODRIVER': 'dummy', 'DYLD_PRINT_LIBRARIES': '1'},
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(self.app / 'Contents/Frameworks/libSDL3.dylib'), result.stderr)

    def test_signing_preparation_clears_attributes_on_read_only_resources(self):
        resource = self.app / 'Contents/Info.plist'
        resource.write_text('resource')
        subprocess.run(['xattr', '-w', 'com.isaac.packaging-test', 'test', str(resource)], check=True)
        resource.chmod(0o444)
        self.module.prepare_for_signing(self.app)
        self.assertNotIn('com.isaac.packaging-test', subprocess.check_output(['xattr', str(resource)], text=True))
        self.assertTrue(resource.stat().st_mode & 0o200)

    def test_verifier_rejects_external_symlinks(self):
        self.make_chain()
        self.module.Relocator(self.app).relocate()
        (self.app / 'Contents/Resources').mkdir()
        (self.app / 'Contents/Resources/external').symlink_to(self.external, target_is_directory=True)
        with self.assertRaisesRegex(self.module.PackageError, 'symlink|Symlink'):
            self.module.verify_bundle(self.app)


if __name__ == '__main__':
    unittest.main()
