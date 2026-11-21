import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('windows_closure',Path(__file__).with_name('windows-dll-closure.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class PrivateRuntimeTests(unittest.TestCase):
    def collect_import(self,relative,imported):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stage=root/'stage';prefix=root/'ucrt';system=root/'Windows'
            (system/'System32').mkdir(parents=True);(prefix/'bin').mkdir(parents=True)
            binary=stage/relative;binary.parent.mkdir(parents=True,exist_ok=True);binary.write_bytes(b'PE')
            def inspect(args,**kwargs):
                return SimpleNamespace(stdout='file format pei-x86-64\nDLL Name: '+imported)
            with patch.dict(os.environ,{'SystemRoot':str(system)}),patch.object(module.subprocess,'run',side_effect=inspect):
                module.collect(stage,prefix,Path('objdump'))
            self.assertFalse((stage/'nvcuda.dll').exists())

    def test_cuda_worker_uses_the_installed_driver_without_bundling_it(self):
        self.collect_import('subtitles/ggml-cuda.dll','nvcuda.dll')

    def test_cpu_worker_and_editor_cannot_require_the_optional_driver(self):
        for binary in ('subtitles/cpu/ggml.dll','IsaacVideoEditor.exe'):
            with self.subTest(binary=binary),self.assertRaises(RuntimeError):
                self.collect_import(binary,'nvcuda.dll')

    def test_cuda_worker_still_requires_its_other_runtime_libraries(self):
        with self.assertRaises(RuntimeError):
            self.collect_import('subtitles/ggml-cuda.dll','cublasLt64_12.dll')

    def test_private_worker_dependencies_stay_beside_each_worker(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);stage=root/'stage';prefix=root/'ucrt';system=root/'Windows';(system/'System32').mkdir(parents=True);(prefix/'bin').mkdir(parents=True)
            for folder,payload in [(stage/'subtitles',b'CUDA'),(stage/'subtitles/cpu',b'CPU')]:
                folder.mkdir(parents=True,exist_ok=True);(folder/'whisper-cli.exe').write_bytes(b'EXE');(folder/'ggml.dll').write_bytes(payload)
            def inspect(args,**kwargs):
                imports='DLL Name: ggml.dll' if Path(args[-1]).suffix=='.exe' else ''
                return SimpleNamespace(stdout='file format pei-x86-64\n'+imports)
            with patch.dict(os.environ,{'SystemRoot':str(system)}),patch.object(module.subprocess,'run',side_effect=inspect):module.collect(stage,prefix,Path('objdump'))
            self.assertFalse((stage/'ggml.dll').exists())
            self.assertEqual((stage/'subtitles/ggml.dll').read_bytes(),b'CUDA')
            self.assertEqual((stage/'subtitles/cpu/ggml.dll').read_bytes(),b'CPU')

if __name__=='__main__':unittest.main()
