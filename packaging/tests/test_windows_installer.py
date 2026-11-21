"""Exercise installer compilation boundaries without requiring Windows."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'package-windows-installer.py'


class InstallerBuilderTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('installer_builder', SCRIPT)
        self.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.builder)
        self.temp = tempfile.TemporaryDirectory(prefix='installer paths with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / 'payload'
        self.payload.mkdir()
        for name in ('IsaacVideoEditor.exe', 'IsaacVideoEditorUpdater.exe', '.isaac-portable', 'isaac-package-files.txt'):
            (self.payload / name).write_text('payload')
        self.output = self.root / 'downloads'
        self.compiler = self.root / 'fake compiler'
        self.compiler.write_text(f'#!{sys.executable}\nimport json, pathlib, sys\n'
            'args = dict(arg[2:].split("=", 1) for arg in sys.argv[1:-1])\n'
            'out = pathlib.Path(args["OutputDir"])\n'
            '(out / "arguments.json").write_text(json.dumps(args))\n'
            '(out / ("IsaacVideoEditor-" + args["PackageVersion"] + "-windows-x86_64-Setup.exe")).write_bytes(b"setup")\n')
        self.compiler.chmod(0o755)

    def package(self, label='0.6', version='0.6'):
        return self.builder.package(self.payload, self.output, label, version, self.compiler)

    def test_compiles_same_payload_with_paths_containing_spaces(self):
        result = self.package('validation-42', 'v0.6.1')
        self.assertEqual(result.name, 'IsaacVideoEditor-validation-42-windows-x86_64-Setup.exe')
        self.assertEqual(result.read_bytes(), b'setup')
        args = json.loads((self.output / 'arguments.json').read_text())
        self.assertEqual(args['PayloadDir'], str(self.payload.resolve()))
        self.assertEqual(args['AppVersion'], 'v0.6.1')
        self.assertEqual(args['FileVersion'], '0.6.1.0')

    def test_rejects_invalid_versions_before_creating_output(self):
        for label, version in (('../escape', '0.6'), ('-flag', '0.6'), ('0.6', 'x'), ('0.6', '1.2.3.4.5'), ('0.6', '70000.1'), ('0.6', '0.6\nextra')):
            with self.subTest(label=label, version=version):
                with self.assertRaises(ValueError):
                    self.package(label, version)
                self.assertFalse(self.output.exists())

    def test_refuses_to_replace_existing_installer(self):
        original = self.package()
        with self.assertRaises(FileExistsError):
            self.package()
        self.assertEqual(original.read_bytes(), b'setup')

    def test_requires_complete_payload(self):
        (self.payload / '.isaac-portable').unlink()
        with self.assertRaises(FileNotFoundError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_reports_compiler_failure(self):
        self.compiler.write_text(f'#!{sys.executable}\nraise SystemExit(7)\n')
        with self.assertRaises(subprocess.CalledProcessError):
            self.package()

    def test_requires_compiler_to_produce_installer(self):
        self.compiler.write_text(f'#!{sys.executable}\nraise SystemExit(0)\n')
        with self.assertRaises(FileNotFoundError):
            self.package()


if __name__ == '__main__':
    unittest.main()
