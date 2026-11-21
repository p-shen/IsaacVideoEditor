#!/usr/bin/env python3
"""Prepare a pinned standalone Whisper runtime; never modify an existing output."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

SOURCE_URL='https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.5.tar.gz'
SOURCE_SHA='ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720'
WINDOWS_URL='https://github.com/ggml-org/whisper.cpp/releases/download/b5454/whisper-bin-win-cuda-12.4.0-x64.zip'
WINDOWS_SHA='afef0b881c500958921c3f5523b50e59ee2ec9b6f5cbd25b324c51ed308a957a'
CPU_URL='https://github.com/ggml-org/whisper.cpp/releases/download/b5454/whisper-bin-x64.zip'
CPU_SHA='6ba69e3482d7826214f90a6a9c84ca07782aec1e1d0c6a7c30c994fd5d816ccb'

class PreparationError(RuntimeError):
    pass

def digest(path):
    result=hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda:file.read(1024*1024),b''):result.update(block)
    return result.hexdigest()

def verify(path,expected):
    if digest(path)!=expected:raise PreparationError('Whisper download failed its SHA-256 check')

def download(url,path,expected=None):
    print('Downloading '+url.rsplit('/',1)[-1],flush=True)
    with urllib.request.urlopen(url,timeout=60) as response,path.open('xb') as file:
        total=0
        for block in iter(lambda:response.read(1024*1024),b''):
            total+=len(block)
            if total>2*1024**3:raise PreparationError('Runtime download exceeded its size limit')
            file.write(block)
    if expected:verify(path,expected)

def safe_target(root,name):
    relative=PurePosixPath(name)
    if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:raise PreparationError('Unsafe archive path: '+name)
    target=root.joinpath(*relative.parts)
    try:target.resolve().relative_to(root.resolve())
    except ValueError:raise PreparationError('Archive path escaped its staging directory')
    return target

def extract_zip(archive,root):
    root.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as file:
        if sum(x.file_size for x in file.infolist())>4*1024**3:raise PreparationError('Runtime archive is too large')
        for member in file.infolist():
            target=safe_target(root,member.filename)
            if stat.S_ISLNK(member.external_attr>>16):raise PreparationError('Archive links are unsupported')
            if member.is_dir():target.mkdir(parents=True,exist_ok=True);continue
            target.parent.mkdir(parents=True,exist_ok=True)
            with file.open(member) as source,target.open('xb') as output:shutil.copyfileobj(source,output)

def stage_windows(source,output):
    engines=list(source.rglob('whisper-cli.exe'))
    if len(engines)!=1:raise PreparationError('Expected exactly one whisper-cli.exe')
    if output.exists():raise PreparationError('Runtime output already exists')
    libraries=list(source.rglob('*.dll'))
    notices=[p for p in source.rglob('*') if p.is_file() and p.name.lower().startswith(('license','copying','notice','eula'))]
    output.mkdir(parents=True);shutil.copy2(engines[0],output/'whisper-cli.exe')
    for file in libraries:
        target=output/file.name
        if target.exists() and digest(target)!=digest(file):raise PreparationError('Conflicting runtime DLL: '+file.name)
        shutil.copy2(file,target)
    (output/'licenses').mkdir()
    for i,file in enumerate(notices):
        target=output/'licenses'/('whisper-'+file.name)
        if target.exists():target=target.with_name(str(i)+'-'+target.name)
        shutil.copy2(file,target)

def utf8_manifest(original=None):
    root=ET.fromstring(original) if original else ET.fromstring('<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"/>')
    application=root.find('{urn:schemas-microsoft-com:asm.v3}application')
    if application is None:application=ET.SubElement(root,'{urn:schemas-microsoft-com:asm.v3}application')
    settings=application.find('{urn:schemas-microsoft-com:asm.v3}windowsSettings')
    if settings is None:settings=ET.SubElement(application,'{urn:schemas-microsoft-com:asm.v3}windowsSettings')
    tag='{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage'
    page=settings.find(tag)
    if page is None:page=ET.SubElement(settings,tag)
    page.text='UTF-8'
    return ET.tostring(root,encoding='utf-8',xml_declaration=True)

def activate_utf8(executable,work):
    # The pinned CLI uses narrow argv/file APIs; a UTF-8 manifest preserves Windows user/media paths.
    tool=shutil.which('mt.exe')
    if not tool:
        kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10/bin'
        candidates=sorted(kits.glob('*/x64/mt.exe'))
        if candidates:tool=str(candidates[-1])
    if not tool:raise PreparationError('Windows SDK mt.exe is required to enable Unicode paths in the Whisper worker')
    manifest=work/('manifest-'+executable.parent.name+'.xml')
    result=subprocess.run([tool,'-nologo','-inputresource:'+str(executable)+';#1','-out:'+str(manifest)],capture_output=True)
    original=manifest.read_bytes() if result.returncode==0 else None
    manifest.write_bytes(utf8_manifest(original))
    subprocess.run([tool,'-nologo','-manifest',str(manifest),'-outputresource:'+str(executable)+';#1'],check=True)

def copy_msvc_runtime(runtime):
    candidates=[]
    for variable,default in [('ProgramFiles','C:/Program Files'),('ProgramFiles(x86)','C:/Program Files (x86)')]:
        base=Path(os.environ.get(variable,default))/'Microsoft Visual Studio'
        candidates.extend(base.glob('*/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT'))
    if not candidates:raise PreparationError('The Visual Studio 2022 x64 redistributable directory is required to package the native worker')
    redist=sorted(candidates)[-1]
    for folder in (runtime,runtime/'cpu'):
        for file in redist.glob('*.dll'):shutil.copy2(file,folder/file.name)
    download('https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/',runtime/'licenses/Microsoft-Visual-C-runtime-license.html')

def prepare(output,target,source_archive=None):
    output=output.resolve()
    if output.exists():raise PreparationError('Choose a new runtime output directory')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.whisper-prepare-',dir=output.parent) as directory:
        work=Path(directory);stage=work/'runtime'
        if target=='windows':
            for name,url,sha,destination in [('cuda',WINDOWS_URL,WINDOWS_SHA,stage),('cpu',CPU_URL,CPU_SHA,stage/'cpu')]:
                archive=work/(name+'.zip');download(url,archive,sha);extracted=work/name;extract_zip(archive,extracted);stage_windows(extracted,destination)
                activate_utf8(destination/'whisper-cli.exe',work)
            license_file=stage/'licenses/NVIDIA-CUDA-12.4-EULA.html'
            download('https://docs.nvidia.com/cuda/archive/12.4.0/eula/index.html',license_file)
            if not any((stage/'licenses').glob('whisper-*')):
                download('https://raw.githubusercontent.com/ggml-org/whisper.cpp/b5454/LICENSE',stage/'licenses/whisper-LICENSE')
            copy_msvc_runtime(stage)
            version='b5454';device='CUDA 12.4 / CPU'
        else:
            archive=Path(source_archive).resolve() if source_archive else work/'source.tar.gz'
            if source_archive:verify(archive,SOURCE_SHA)
            else:download(SOURCE_URL,archive,SOURCE_SHA)
            source=work/'source';source.mkdir()
            with tarfile.open(archive) as file:
                for member in file.getmembers():
                    safe_target(source,member.name)
                    if not(member.isfile() or member.isdir()):raise PreparationError('Source archive contains an unsupported link')
                file.extractall(source)
            trees=list(source.glob('whisper.cpp-*'))
            if len(trees)!=1:raise PreparationError('Whisper source root is missing')
            tree=trees[0];build=work/'build'
            subprocess.run(['cmake','-S',str(tree),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_SHARED_LIBS=OFF','-DGGML_BACKEND_DL=OFF','-DGGML_NATIVE=OFF','-DGGML_OPENMP=OFF','-DGGML_METAL_EMBED_LIBRARY=ON','-DWHISPER_BUILD_TESTS=OFF','-DWHISPER_BUILD_SERVER=OFF'],check=True)
            subprocess.run(['cmake','--build',str(build),'--target','whisper-cli','--parallel','4'],check=True)
            stage.mkdir();shutil.copy2(build/'bin/whisper-cli',stage/'whisper-cli');(stage/'licenses').mkdir();shutil.copy2(tree/'LICENSE',stage/'licenses/whisper-LICENSE')
            version='v1.9.5';device='Metal / CPU' if target=='macos' else 'CPU'
        (stage/'runtime.json').write_text(json.dumps({'engine':'whisper.cpp','version':version,'platform':target,'device':device,'source':'https://github.com/ggml-org/whisper.cpp','files':{str(p.relative_to(stage)):digest(p) for p in sorted(stage.rglob('*')) if p.is_file()}},indent=2)+'\n')
        stage.rename(output)
    print('Whisper runtime ready: '+str(output),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    parser.add_argument('--platform',choices=['windows','macos','linux'],default={'Windows':'windows','Darwin':'macos'}.get(platform.system(),'linux'))
    parser.add_argument('--source-archive',type=Path,help='Previously downloaded, checksum-verified v1.9.5 source archive (Mac/Linux)')
    args=parser.parse_args()
    try:prepare(args.output,args.platform,args.source_archive)
    except (PreparationError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')
