#!/usr/bin/env python3
"""Build an ad-hoc-signed, relocatable macOS arm64 ZIP using installed Homebrew deps.

Usage: python3 packaging/package-macos.py BUILD_DIR OUTPUT_DIR VERSION
Only a private temporary staging directory is modified; the build app is untouched.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


class PackageError(RuntimeError):
    pass


def run(*args):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise PackageError(f"Command failed: {' '.join(map(str, args))}\n{result.stdout}{result.stderr}")
    return result.stdout.strip()


def is_system(reference):
    return reference.startswith(('/System/Library/', '/usr/lib/'))


def inside(path, root):
    return path.resolve().is_relative_to(root.resolve())


def macho_files(root):
    magic = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
             b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
             b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
    for path in sorted(root.rglob('*')):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as file:
                if file.read(4) in magic:
                    yield path


def load_commands(path):
    dependencies, rpaths, identity = [], [], None
    command = None
    for line in run('otool', '-l', path).splitlines():
        line = line.strip()
        if line.startswith('cmd '):
            command = line.split()[1]
        elif command == 'LC_RPATH' and line.startswith('path '):
            rpaths.append(line[5:].rsplit(' (offset ', 1)[0])
        elif line.startswith('name '):
            name = line[5:].rsplit(' (offset ', 1)[0]
            if command == 'LC_ID_DYLIB':
                identity = name
            elif command in {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB', 'LC_REEXPORT_DYLIB',
                             'LC_LOAD_UPWARD_DYLIB', 'LC_LAZY_LOAD_DYLIB'}:
                dependencies.append(name)
    return list(dict.fromkeys(dependencies)), list(dict.fromkeys(rpaths)), identity


class Relocator:
    def __init__(self, app, sources=None):
        self.app = app.resolve()
        self.frameworks = self.app / 'Contents/Frameworks'
        self.executable = self.app / 'Contents/MacOS/IsaacVideoEditor'
        self.sources = {dst.resolve(): src.resolve() for dst, src in (sources or {}).items()}
        self.by_source = {src: dst for dst, src in self.sources.items()}
        self.origins = set(self.sources.values())
        self.inherited_rpaths = {}

    def expand(self, reference, owner):
        if reference == '@loader_path':
            return owner.parent
        if reference.startswith('@loader_path/'):
            return owner.parent / reference[len('@loader_path/'):]
        if reference == '@executable_path':
            return self.executable.parent
        if reference.startswith('@executable_path/'):
            return self.executable.parent / reference[len('@executable_path/'):]
        if reference.startswith('/'):
            return Path(reference)
        return None

    def search_paths(self, owner, rpaths):
        paths = []
        for location in (owner, self.sources.get(owner, owner)):
            paths.extend(filter(None, (self.expand(path, location) for path in rpaths)))
        if owner != self.executable:
            _, executable_rpaths, _ = load_commands(self.executable)
            paths.extend(filter(None, (self.expand(path, self.executable) for path in executable_rpaths)))
        paths.extend(self.inherited_rpaths.get(owner, []))
        paths.append(self.frameworks)
        return list(dict.fromkeys(path.resolve() for path in paths))

    def resolve(self, reference, owner, search_paths):
        candidates = []
        if reference.startswith('@rpath/'):
            candidates.extend(path / reference[len('@rpath/'):] for path in search_paths)
        else:
            for location in (owner, self.sources.get(owner, owner)):
                candidate = self.expand(reference, location)
                if candidate:
                    candidates.append(candidate)
        for path in candidates:
            if path.is_file():
                return path.resolve()
        raise PackageError(f'Unresolved dependency {reference} required by {owner}')

    def copy_dependency(self, source):
        source = source.resolve()
        self.origins.add(source)
        if inside(source, self.app):
            return source
        if source in self.by_source:
            return self.by_source[source]
        framework = next((parent for parent in source.parents if parent.suffix == '.framework'), None)
        if framework:
            target_root = self.frameworks / framework.name
            target = target_root / source.relative_to(framework)
            if not target_root.exists():
                shutil.copytree(framework, target_root, symlinks=True,
                                ignore=shutil.ignore_patterns('Headers', '*.prl', '*_debug*'))
            if not target.is_file():
                raise PackageError(f'Conflicting framework: {source} and {target_root}')
            for binary in macho_files(target_root):
                original = framework / binary.relative_to(target_root)
                self.sources[binary.resolve()] = original.resolve()
                self.by_source[original.resolve()] = binary.resolve()
                self.origins.add(original.resolve())
        else:
            target = self.frameworks / source.name
            if target.exists():
                raise PackageError(f'Conflicting dependency filename: {source} and {target}')
            shutil.copy2(source, target)
        target = target.resolve()
        self.sources[target] = source
        self.by_source[source] = target
        return target

    def relocate(self):
        self.frameworks.mkdir(parents=True, exist_ok=True)
        queue = list(macho_files(self.app))
        processed = set()
        while queue:
            owner = queue.pop(0).resolve()
            if owner in processed:
                continue
            processed.add(owner)
            dependencies, rpaths, identity = load_commands(owner)
            search_paths = self.search_paths(owner, rpaths)
            changes = []
            for reference in dependencies:
                if is_system(reference):
                    continue
                target = self.copy_dependency(self.resolve(reference, owner, search_paths))
                self.inherited_rpaths.setdefault(target, search_paths)
                changes.extend(['-change', reference,
                                '@loader_path/' + os.path.relpath(target, owner.parent)])
                if target not in processed:
                    queue.append(target)
            if identity:
                changes.extend(['-id', '@loader_path/' + owner.name])
            # Every load reference is now direct; inherited build-machine rpaths are unnecessary.
            for rpath in rpaths:
                changes.extend(['-delete_rpath', rpath])
            if changes:
                owner.chmod(owner.stat().st_mode | 0o200)
                run('install_name_tool', *changes, owner)
            # A copied framework can contain more than its main binary.
            queue.extend(path for path in macho_files(self.app) if path.resolve() not in processed)
        return self.origins


def verify_bundle(app):
    """Reject dependencies or symlinks that would escape a relocated app."""
    app = app.resolve()
    for path in app.rglob('*'):
        if path.is_symlink() and (not path.exists() or not inside(path, app)):
            raise PackageError(f'External or broken symlink: {path}')
    count = 0
    for owner in macho_files(app):
        count += 1
        dependencies, rpaths, identity = load_commands(owner)
        if any(path.startswith('/') for path in rpaths):
            raise PackageError(f'External runtime search path in {owner}: {rpaths}')
        if identity and not identity.startswith(('@loader_path/', '@rpath/')):
            raise PackageError(f'External library identity in {owner}: {identity}')
        for reference in dependencies:
            if is_system(reference):
                continue
            if not reference.startswith('@loader_path/'):
                raise PackageError(f'External or unresolved dependency in {owner}: {reference}')
            target = owner.parent / reference[len('@loader_path/'):]
            if not target.is_file() or not inside(target, app):
                raise PackageError(f'Missing or external dependency in {owner}: {reference}')
    if not count:
        raise PackageError(f'No Mach-O binaries found in {app}')
    return count


def keg_for(source):
    return next((parent for parent in source.resolve().parents
                 if (parent / 'INSTALL_RECEIPT.json').is_file()), None)


def add_sdl_compat_runtime(app, owner, sources):
    for reference in load_commands(owner)[0]:
        if not reference.startswith('/'):
            continue
        keg = keg_for(Path(reference))
        if keg and keg.parent.name == 'sdl2-compat':
            # sdl2-compat dlopens this name beside its own dylib; otool cannot discover it.
            source = Path(run('brew', '--prefix', 'sdl3')) / 'lib/libSDL3.dylib'
            if not source.is_file():
                raise PackageError(f'Missing sdl2-compat runtime: {source}')
            target = app / 'Contents/Frameworks/libSDL3.dylib'
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            sources[target] = source
            return


def add_notices(app, origins, repo):
    destination = app / 'Contents/Resources/licenses'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo / 'LICENSE', destination / 'IsaacVideoEditor-LICENSE.txt')
    kegs = {keg_for(path) for path in origins}
    kegs.discard(None)
    if not kegs:
        raise PackageError('Cannot identify bundled Homebrew dependency licenses')
    notices = ['IsaacVideoEditor bundles the following Homebrew dependencies.',
               'Per-dependency license notices, upstream source locations, and installed',
               'license texts are in the adjacent folders. Qt upstream SPDX notices include',
               'copyright and licensing details for its embedded third-party components.', '']
    for keg in sorted(kegs):
        formula, version = keg.parent.name, keg.name
        folder = destination / (formula + '-' + version)
        folder.mkdir()
        candidates = [path for path in keg.iterdir() if path.is_file() and
                      re.match(r'(?i)(licen[cs]e|copying|copyright|notice|authors)', path.name)]
        candidates.extend(path for path in (keg / 'share/qt/sbom').glob('*') if path.is_file())
        candidates.extend(path for path in (keg / 'share/qt6/sbom').glob('*') if path.is_file())
        sbom_path = keg / 'sbom.spdx.json'
        if not sbom_path.is_file():
            raise PackageError(f'Missing Homebrew license/source metadata: {keg}')
        sbom = json.loads(sbom_path.read_text())
        package = next((item for item in sbom.get('packages', []) if item.get('name') == formula), None)
        if not package or package.get('licenseConcluded', 'NOASSERTION') == 'NOASSERTION':
            raise PackageError(f'Missing dependency license declaration: {keg}')
        candidates.append(sbom_path)
        for path in dict.fromkeys(candidates):
            shutil.copy2(path, folder / path.name)
        notices.append(f"{formula} {version}: {package['licenseConcluded']}")
        notices.append(f"Source: {package.get('downloadLocation', 'See sbom.spdx.json')}")
    (destination / 'THIRD-PARTY-NOTICES.txt').write_text('\n'.join(notices) + '\n')


def qt_origins(app, qt_paths):
    sources = {}
    mappings = {'Contents/Frameworks': qt_paths['QT_INSTALL_LIBS'],
                'Contents/PlugIns': qt_paths['QT_INSTALL_PLUGINS'],
                'Contents/Resources/qml': qt_paths['QT_INSTALL_QML']}
    for relative, source_root in mappings.items():
        deployed = app / relative
        if not deployed.exists():
            continue
        for path in deployed.rglob('*'):
            if path.is_file():
                source = source_root / path.relative_to(deployed)
                if source.is_file():
                    sources[path.resolve()] = source.resolve()
    return sources


def prepare_for_signing(app):
    # Homebrew framework resources can be read-only; xattr and codesign need writable copies.
    for path in [app, *app.rglob('*')]:
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | 0o200)
    run('xattr', '-cr', app)


def package(build_dir, output_dir, version):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,99}', version):
        raise PackageError('VERSION must contain only safe filename characters: letters, numbers, . _ + -')
    if sys.platform != 'darwin':
        raise PackageError('macOS packaging requires macOS')
    if run('uname', '-m') != 'arm64':
        raise PackageError('This package targets macOS arm64; build on an Apple Silicon Mac')
    repo = Path(__file__).resolve().parent.parent
    source_app = build_dir.resolve() / 'IsaacVideoEditor.app'
    if not (source_app / 'Contents/MacOS/IsaacVideoEditor').is_file():
        raise PackageError(f'Missing built app: {source_app}')
    if not (source_app / 'Contents/MacOS/IsaacVideoEditorUpdater').is_file():
        raise PackageError(f'Missing update installer in built app: {source_app}')
    for tool in ('brew', 'otool', 'install_name_tool', 'codesign', 'ditto', 'xattr'):
        if not shutil.which(tool):
            raise PackageError(f'Required packaging tool not found: {tool}')
    mlt = Path(run('brew', '--prefix', 'mlt')).resolve()
    ffmpeg = Path(run('brew', '--prefix', 'ffmpeg')).resolve()
    qt = Path(run('brew', '--prefix', 'qt'))
    deployqt = qt / 'bin/macdeployqt'
    qtpaths = qt / 'bin/qtpaths'
    if not deployqt.is_file() or not qtpaths.is_file():
        raise PackageError(f'Qt deployment tools not found in {qt}')
    qt_paths = {key: Path(run(qtpaths, '--query', key))
                for key in ('QT_INSTALL_LIBS', 'QT_INSTALL_PLUGINS', 'QT_INSTALL_QML')}
    mlt_plugins = next((path for path in (mlt / 'lib/mlt-7', mlt / 'lib/mlt') if path.is_dir()), None)
    mlt_data = next((path for path in (mlt / 'share/mlt-7', mlt / 'share/mlt') if path.is_dir()), None)
    if not mlt_plugins or not mlt_data:
        raise PackageError(f'MLT plugins/data not found under {mlt}')
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    artifact = output_dir / f'IsaacVideoEditor-{version}-macos-arm64.zip'
    if artifact.exists():
        raise PackageError(f'Refusing to overwrite existing artifact: {artifact}')
    with tempfile.TemporaryDirectory(prefix='isaac-macos-') as staging:
        app = Path(staging) / source_app.name
        shutil.copytree(source_app, app, symlinks=True)
        executable = app / 'Contents/MacOS/IsaacVideoEditor'
        initial_dependencies, _, _ = load_commands(executable)
        origins = {Path(path).resolve() for path in initial_dependencies if path.startswith('/') and not is_system(path)}
        print('Deploying Qt frameworks, plugins, and QML imports...', flush=True)
        output = run(deployqt, app, '-qmldir=' + str(repo / 'qml'), '-no-codesign', '-verbose=1')
        if output:
            print(output, flush=True)
        # macdeployqt deploys Cocoa only; the package smoke check also needs offscreen.
        offscreen = qt_paths['QT_INSTALL_PLUGINS'] / 'platforms/libqoffscreen.dylib'
        if not offscreen.is_file():
            raise PackageError(f'Missing Qt offscreen platform plugin: {offscreen}')
        platform_target = app / 'Contents/PlugIns/platforms'
        platform_target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(offscreen, platform_target / offscreen.name)
        (app / 'Contents/Resources/qt.conf').write_text(
            '[Paths]\nPrefix=..\nPlugins=PlugIns\nQmlImports=Resources/qml\n')
        sources = qt_origins(app, qt_paths)
        origins.update(sources.values())
        # macdeployqt can copy MLT too; preserve aliases to avoid loading two MLT runtimes.
        for reference in initial_dependencies:
            source = Path(reference)
            if not reference.startswith('/') or is_system(reference):
                continue
            framework = next((parent for parent in source.parents if parent.suffix == '.framework'), None)
            relative = Path(framework.name) / source.relative_to(framework) if framework else Path(source.name)
            target = app / 'Contents/Frameworks' / relative
            if target.is_file():
                sources[target.resolve()] = source.resolve()
        for tool in ('ffmpeg', 'ffprobe'):
            source = ffmpeg / 'bin' / tool
            if not source.is_file():
                raise PackageError(f'Missing bundled tool: {source}')
            target = executable.parent / tool
            shutil.copy2(source, target)
            sources[target] = source
        plugin_target = app / 'Contents/PlugIns/mlt-7'
        plugin_target.mkdir(parents=True, exist_ok=True)
        # These modules provide every service used by the editor (including volume/audiolevel).
        for module in ('core', 'avformat', 'normalize', 'resample', 'sdl2', 'xml'):
            matches = [path for path in mlt_plugins.glob('libmlt' + module + '.*')
                       if path.suffix in ('.so', '.dylib')]
            if len(matches) != 1:
                raise PackageError(f'Expected one MLT {module} plugin in {mlt_plugins}; found {matches}')
            target = plugin_target / matches[0].name
            shutil.copy2(matches[0], target)
            sources[target] = matches[0]
            if module == 'sdl2':
                add_sdl_compat_runtime(app, matches[0], sources)
        shutil.copytree(mlt_data, app / 'Contents/Resources/mlt-7', symlinks=False)
        origins.add(mlt / 'COPYING')
        print('Bundling and relocating all non-system dependencies...', flush=True)
        origins.update(Relocator(app, sources).relocate())
        add_notices(app, origins, repo)
        count = verify_bundle(app)
        print(f'Verified dependency closure for {count} Mach-O binaries; signing...', flush=True)
        prepare_for_signing(app)
        for path in sorted(macho_files(app), key=lambda path: len(path.parts), reverse=True):
            run('codesign', '--force', '--sign', '-', '--timestamp=none', path)
        containers = [path for path in app.rglob('*') if path.is_dir() and not path.is_symlink()
                      and path.suffix in ('.framework', '.app', '.bundle', '.xpc')]
        for path in sorted(containers, key=lambda path: len(path.parts), reverse=True):
            run('codesign', '--force', '--sign', '-', '--timestamp=none', path)
        run('codesign', '--force', '--sign', '-', '--timestamp=none', app)
        run('codesign', '--verify', '--deep', '--strict', app)
        verify_bundle(app)
        archive = Path(staging) / artifact.name
        run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, archive)
        shutil.move(archive, artifact)
    print(artifact)
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path, metavar='BUILD_DIR')
    parser.add_argument('output_dir', type=Path, metavar='OUTPUT_DIR')
    parser.add_argument('version', metavar='VERSION')
    args = parser.parse_args()
    try:
        package(args.build_dir, args.output_dir, args.version)
    except (PackageError, OSError, ValueError) as error:
        parser.exit(1, f'Packaging failed: {error}\n')


if __name__ == '__main__':
    main()
