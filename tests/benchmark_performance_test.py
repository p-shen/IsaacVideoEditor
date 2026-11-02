"""Check generated benchmark snapshots against the actual application loader."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("benchmark", root / "tools/benchmark_performance.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def main():
    editor = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="isaac-benchmark-schema-") as folder:
        for workload in ("plain", "overlay", "text"):
            project = Path(folder) / f"{workload}.isaac.json"
            report = Path(folder) / f"{workload}.json"
            project.write_text(json.dumps(benchmark.project(root / "tests/fixtures/seek.mp4", 160, 90, 1, workload)))
            result = subprocess.run([str(editor), "--performance-report", str(project), str(report)],
                                    env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "SDL_AUDIODRIVER": "dummy"},
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (workload, result.stdout, result.stderr)
            data = json.loads(report.read_text())
            assert len(data["seeks"]) == 10, workload
            assert data["editing"]["preview"]["cacheHits"] > 0, workload
    print("All three generated project formats loaded and delivered cold/warm frames")


if __name__ == "__main__":
    main()
