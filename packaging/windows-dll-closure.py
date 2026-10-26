#!/usr/bin/env python3
"""Copy only the transitive UCRT64 imports of all staged PE binaries."""
from collections import deque
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def collect(stage, prefix, objdump):
    available = {p.name.lower(): p for p in (prefix / "bin").iterdir() if p.suffix.lower() == ".dll"}
    deployed = {p.name.lower(): p for p in stage.iterdir() if p.suffix.lower() == ".dll"}
    system = Path(os.environ.get("SystemRoot", os.environ.get("SYSTEMROOT", r"C:\Windows"))) / "System32"
    system_dlls = {p.name.lower() for p in system.iterdir() if p.suffix.lower() == ".dll"}
    pending = deque(p for p in stage.rglob("*") if p.suffix.lower() in (".exe", ".dll"))
    seen = set()
    while pending:
        binary = pending.popleft()
        if binary in seen:
            continue
        seen.add(binary)
        output = subprocess.run([str(objdump), "-p", str(binary)], check=True, capture_output=True, text=True).stdout
        if "pei-x86-64" not in output:
            raise RuntimeError(f"Expected an x86_64 Windows binary: {binary}")
        for imported in re.findall(r"DLL Name:\s*([^\r\n]+)", output):
            name = imported.strip()
            key = name.lower()
            if key == "msys-2.0.dll" or any(char in name for char in "/\\:"):
                raise RuntimeError(f"Unsupported dependency {name} imported by {binary}")
            if key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if key in deployed:
                continue
            if key in available:
                source = available[key]
                target = stage / source.name
                shutil.copy2(source, target)
                deployed[key] = target
                pending.append(target)
            elif key not in system_dlls:
                raise RuntimeError(f"Unresolved dependency {name} imported by {binary}; install its UCRT64 package")
    print(f"Checked {len(seen)} PE binaries; deployed {len(deployed)} runtime DLLs")


if __name__ == "__main__":
    try:
        if len(sys.argv) != 4:
            raise RuntimeError("Usage: windows-dll-closure.py STAGE UCRT64_PREFIX OBJDUMP")
        collect(*(Path(argument).resolve() for argument in sys.argv[1:]))
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(f"Windows dependency collection failed: {error}")
