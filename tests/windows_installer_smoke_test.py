#!/usr/bin/env python3
"""Install, update and uninstall on a disposable Windows CI runner only."""
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, timeout=180, **kwargs)


def wait_for(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while not predicate():
        require(time.monotonic() < deadline, 'Timed out waiting for installer state')
        time.sleep(.1)


def main(installer, archive):
    require(sys.platform == 'win32' and os.environ.get('GITHUB_ACTIONS') == 'true',
            'Run this only on a disposable Windows GitHub Actions runner')
    import winreg
    installer, archive = Path(installer).resolve(), Path(archive).resolve()
    with tempfile.TemporaryDirectory(prefix='Isaac setup check ') as directory:
        root = Path(directory).resolve()
        app = Path(os.environ['LOCALAPPDATA']) / 'Programs/IsaacVideoEditor'
        require(not app.exists(), 'The disposable runner already has an installation')
        log = root / 'setup.log'

        def install(desktop=False):
            tasks = ['/TASKS=desktopicon'] if desktop else []
            run(installer, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
                *tasks, f'/LOG={log}')

        install()
        executable = app / 'IsaacVideoEditor.exe'
        require(executable.is_file(), 'Main executable was not installed')
        key_name = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{2A5AE69D-7194-4F2A-B688-A0C27A690DEF}_is1'
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_name) as key:
            location = winreg.QueryValueEx(key, 'InstallLocation')[0]
            require(Path(location).resolve() == app, 'Uninstall registration points to the wrong installation')
        desktop = ctypes.create_unicode_buffer(32768)
        require(ctypes.windll.shell32.SHGetFolderPathW(None, 0x10, None, 0, desktop) == 0,
                'Could not locate the desktop')
        shortcuts = [Path(os.environ['APPDATA']) / 'Microsoft/Windows/Start Menu/Programs/IsaacVideoEditor.lnk',
                     Path(desktop.value) / 'IsaacVideoEditor.lnk']
        require(shortcuts[0].is_file(), 'Missing Start menu shortcut')
        require(not shortcuts[1].exists(), 'Desktop shortcut should be optional and unchecked by default')
        project = app / 'my project.isaac.json'
        project.write_text('preserve this project')
        install(desktop=True)
        for shortcut in shortcuts:
            require(shortcut.is_file(), f'Missing shortcut: {shortcut}')
            target = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command',
                '(New-Object -ComObject WScript.Shell).CreateShortcut($env:ISAAC_SHORTCUT).TargetPath'],
                env={**os.environ, 'ISAAC_SHORTCUT': str(shortcut)}, text=True, timeout=30).strip()
            require(Path(target).resolve() == executable, f'Incorrect shortcut target: {target}')

        require(project.read_text() == 'preserve this project', 'Reinstall changed a user project')
        uninstall_files = {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in app.glob('unins*') if file.is_file()}
        require(any(name.endswith('.exe') for name in uninstall_files), 'Missing uninstaller')
        manifest = (app / 'isaac-package-files.txt').read_text(encoding='utf-8').splitlines()
        require(not set(uninstall_files).intersection(manifest), 'Installer metadata must remain outside the update manifest')
        run(sys.executable, Path(__file__).with_name('package_smoke_test.py'), executable,
            env={**os.environ, 'ISAAC_CLEAN_RUNTIME': '1'})

        # Exercise the actual updater: it must retain Inno's uninstaller and project.
        prepared = root / 'prepared update'
        prepared.mkdir()
        with zipfile.ZipFile(archive) as source:
            source.extractall(prepared)
        (prepared / 'IsaacVideoEditor').rename(prepared / 'payload')
        added_file = Path('future-runtime/new-runtime.dll')
        (prepared / 'payload' / added_file).parent.mkdir()
        (prepared / 'payload' / added_file).write_text('a dependency introduced by a future update')
        with (prepared / 'payload/isaac-package-files.txt').open('a', encoding='utf-8') as manifest:
            manifest.write(added_file.as_posix() + '\n')
        (prepared / 'ready').write_bytes(b'IsaacVideoEditor update 1\n')
        helper_dir = root / 'helper'
        helper_dir.mkdir()
        helper = helper_dir / 'IsaacVideoEditorUpdater.exe'
        shutil.copy2(app / helper.name, helper)
        temp_path = ctypes.create_unicode_buffer(32768)
        ctypes.windll.kernel32.GetTempPathW(len(temp_path), temp_path)
        print(f'Updater paths: prepared={prepared}; Windows temp={temp_path.value}; Python temp={tempfile.gettempdir()}', flush=True)
        parent = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])
        updater = subprocess.Popen([str(helper), '--install', str(parent.pid), str(prepared), str(app), executable.name],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for(lambda: (prepared / 'armed').exists() or updater.poll() is not None)
            require((prepared / 'armed').exists(), f'Updater did not arm (exit={updater.poll()}): ' +
                    ((prepared / 'error.txt').read_text() if (prepared / 'error.txt').exists() else 'no diagnostic'))
            parent.terminate()
            parent.wait(timeout=10)
            stdout, stderr = updater.communicate(timeout=120)
            require(updater.returncode == 0, f'Update failed: {stdout!r} {stderr!r}')
        finally:
            for process in (parent, updater):
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
            subprocess.run(['taskkill', '/F', '/IM', 'IsaacVideoEditor.exe'], capture_output=True, timeout=20)
        for name, digest in uninstall_files.items():
            require(hashlib.sha256((app / name).read_bytes()).hexdigest() == digest,
                    f'Update changed installer metadata: {name}')
        require(project.read_text() == 'preserve this project', 'Update changed a user project')
        require((app / added_file).is_file(), 'Update did not install the new dependency')
        uninstaller = next(app.glob('unins*.exe'))
        run(uninstaller, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
        wait_for(lambda: not executable.exists())
        require(not (app / added_file).exists(), 'Uninstall left a dependency introduced by an update')
        require(not (app / added_file).parent.exists(), 'Uninstall left an empty updated dependency directory')
        require(project.read_text() == 'preserve this project', 'Uninstall removed a user project')
        require(not any(shortcut.exists() for shortcut in shortcuts), 'Uninstall left application shortcuts')
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_name):
                raise RuntimeError('Uninstall left its Windows Settings registration')
        except FileNotFoundError:
            pass
        project.unlink()
        app.rmdir()
        print('Installer, shortcuts, reinstall, application smoke, in-app update and uninstall checks passed.')


if __name__ == '__main__':
    main(*sys.argv[1:])
