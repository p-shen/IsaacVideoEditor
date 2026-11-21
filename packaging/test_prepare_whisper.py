import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import zipfile
import xml.etree.ElementTree as ET

spec=importlib.util.spec_from_file_location('prepare_whisper',Path(__file__).with_name('prepare-whisper.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class WhisperRuntimeTests(unittest.TestCase):
    def test_msvc_runtime_retains_the_published_license_without_the_blocked_landing_page(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            redist=root/'Microsoft Visual Studio/2022/Enterprise/VC/Redist/MSVC/14.44/x64/Microsoft.VC143.CRT'
            redist.mkdir(parents=True)
            (redist/'vcruntime140.dll').write_bytes(b'runtime DLL')
            runtime=root/'runtime'
            (runtime/'cpu').mkdir(parents=True)
            (runtime/'licenses').mkdir()
            license_bytes=b'published license document'
            def download(url,path,expected=None):
                if '/license-terms/' in url:
                    raise HTTPError(url,403,'Forbidden',{},None)
                self.assertTrue(url.endswith('.docx'))
                self.assertIsNotNone(expected)
                path.write_bytes(license_bytes)
            with patch.dict(module.os.environ,{'ProgramFiles':str(root),'ProgramFiles(x86)':str(root)}), patch.object(module,'download',side_effect=download):
                module.copy_msvc_runtime(runtime)
            for folder in (runtime,runtime/'cpu'):
                self.assertEqual((folder/'vcruntime140.dll').read_bytes(),b'runtime DLL')
            self.assertEqual((runtime/'licenses/Microsoft-Visual-C-runtime-license.docx').read_bytes(),license_bytes)

    def test_application_and_audio_worker_opt_into_utf8_paths(self):
        root=Path(__file__).resolve().parents[1]
        manifest=ET.parse(root/'assets/windows-utf8.manifest')
        page=manifest.find('.//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage')
        self.assertIsNotNone(page)
        self.assertEqual(page.text,'UTF-8')
        self.assertIn('windows-utf8.manifest',(root/'CMakeLists.txt').read_text())
    def test_utf8_manifest_preserves_existing_execution_level(self):
        original=b'<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker"/></requestedPrivileges></security></trustInfo></assembly>'
        result=module.utf8_manifest(original)
        self.assertIn(b'UTF-8',result)
        self.assertIn(b'asInvoker',result)
    def test_checksum_and_unsafe_archive_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);archive=root/'worker.zip'
            with zipfile.ZipFile(archive,'w') as file:file.writestr('../escape.exe',b'bad')
            with self.assertRaises(module.PreparationError):module.verify(archive,'0'*64)
            module.verify(archive,hashlib.sha256(archive.read_bytes()).hexdigest())
            with self.assertRaises(module.PreparationError):module.extract_zip(archive,root/'out')
            self.assertFalse((root.parent/'escape.exe').exists())

    def test_runtime_flattens_native_binaries_and_retains_licenses(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'archive';(source/'Release').mkdir(parents=True)
            (source/'Release/whisper-cli.exe').write_bytes(b'PE')
            (source/'Release/ggml-cuda.dll').write_bytes(b'DLL')
            (source/'LICENSE').write_text('MIT license')
            output=root/'runtime';module.stage_windows(source,output)
            self.assertTrue((output/'whisper-cli.exe').is_file())
            self.assertTrue((output/'ggml-cuda.dll').is_file())
            self.assertTrue((output/'licenses/whisper-LICENSE').is_file())
            with self.assertRaises(module.PreparationError):module.stage_windows(source,output)

    def test_missing_engine_is_not_a_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with self.assertRaises(module.PreparationError):module.stage_windows(root,root/'output')

if __name__=='__main__':unittest.main()
