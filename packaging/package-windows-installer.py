#!/usr/bin/env python3
"""Wrap the staged portable payload in a per-user Inno Setup installer."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def package(payload, output, label, version, compiler=None):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,99}', label):
        raise ValueError('Invalid artifact version')
    match = re.fullmatch(r'v?(\d+(?:\.\d+){0,3})(?:[-+][A-Za-z0-9._-]+)?', version)
    if not match or any(int(part) > 65535 for part in match[1].split('.')):
        raise ValueError('Application version must have one to four numeric components, each at most 65535')
    file_version = '.'.join((match[1].split('.') + ['0'] * 4)[:4])
    payload, output = Path(payload).resolve(), Path(output).resolve()
    for name in ('IsaacVideoEditor.exe', 'IsaacVideoEditorUpdater.exe', '.isaac-portable', 'isaac-package-files.txt'):
        if not (payload / name).is_file():
            raise FileNotFoundError(f'Missing installer payload file: {name}')
    destination = output / f'IsaacVideoEditor-{label}-windows-x86_64-Setup.exe'
    if destination.exists() or destination.is_symlink():
        raise FileExistsError(destination)
    if compiler is None:
        compiler = shutil.which('ISCC.exe') or Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Inno Setup 6/ISCC.exe'
    if not Path(compiler).is_file():
        raise FileNotFoundError('Install Inno Setup 6 or add ISCC.exe to PATH')
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(compiler), f'/DPayloadDir={payload}', f'/DOutputDir={output}',
                    f'/DPackageVersion={label}', f'/DAppVersion={version}', f'/DFileVersion={file_version}',
                    str(Path(__file__).with_name('windows-installer.iss'))], check=True)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise FileNotFoundError(f'Compiler did not produce {destination}')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('label')
    parser.add_argument('version')
    args = parser.parse_args()
    print(package(args.payload, args.output, args.label, args.version))
