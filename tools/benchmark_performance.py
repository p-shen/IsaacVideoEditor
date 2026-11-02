#!/usr/bin/env python3
"""Generate synthetic media and measure the actual editor's editing/export paths.

No user footage is needed. Results and videos stay in a new local output folder.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import threading
import time
import uuid
import xml.etree.ElementTree as ET


class NvidiaSampler:
    """Sample the independent NVIDIA engines; unavailable readings stay absent."""
    def __init__(self):
        self.program = shutil.which("nvidia-smi")
        self.samples, self.errors = [], []
        self.done = threading.Event()
        self.thread = None

    def __enter__(self):
        if self.program:
            self.thread = threading.Thread(target=self.sample, daemon=True)
            self.thread.start()
        return self

    def sample(self):
        started = time.perf_counter()
        while not self.done.is_set():
            try:
                response = subprocess.run([self.program, "-q", "-x"], capture_output=True, text=True, timeout=3, check=True)
                for gpu in ET.fromstring(response.stdout).findall("gpu"):
                    reading = {"seconds": time.perf_counter() - started, "id": gpu.get("id")}
                    for name, path in (("gpuPercent", "utilization/gpu_util"), ("memoryPercent", "utilization/memory_util"),
                                       ("encoderPercent", "utilization/encoder_util"), ("decoderPercent", "utilization/decoder_util"),
                                       ("usedMemoryMiB", "fb_memory_usage/used")):
                        value = (gpu.findtext(path) or "N/A").split()[0]
                        try:
                            reading[name] = float(value)
                        except ValueError:
                            pass
                    self.samples.append(reading)
            except (subprocess.SubprocessError, OSError, ET.ParseError) as error:
                self.errors.append(str(error))
                break
            self.done.wait(1)

    def __exit__(self, *_):
        self.done.set()
        if self.thread:
            self.thread.join(4)

    def report(self, elapsed):
        samples = [sample for sample in self.samples if sample["seconds"] <= elapsed]
        return {"available": bool(samples), "samples": samples, "errors": self.errors,
                "scope": "Whole GPU, including other applications; 1-second interval, short runs may miss peaks"}


def command(arguments, **options):
    return subprocess.run([str(x) for x in arguments], check=True, **options)


def transform(scale=1, x=.5, y=.5):
    result = {"framing": "fit", **{name: 0 for name in ("cropLeft", "cropTop", "cropRight", "cropBottom")}}
    result.update({name: {"value": value, "keys": []} for name, value in
                   (("x", x), ("y", y), ("scale", scale), ("rotation", 0), ("opacity", 1))})
    return result


def project(source, width, height, seconds, workload):
    frames = round(seconds * 60)
    data = {"version": 8, "canvasWidth": width, "canvasHeight": height, "crossfadeSeconds": 0,
            "visualLaneVisible": [True, True, True], "visualOverlays": [], "textOverlays": [],
            "captions": [], "burnCaptions": True,
            "clips": [{"path": str(source), "in": start / 60, "out": end / 60,
                       "hardCutBefore": True, "id": str(uuid.uuid4()), "transform": transform()}
                      for start, end in ((0, frames // 2), (frames // 2, frames))]}
    if workload != "plain":
        data["clips"][0]["transform"] = transform(.8)
        data["visualOverlays"] = [{"path": str(source), "sourceInFrame": 0, "startFrame": frames // 4,
                                  "durationFrames": frames // 2, "lane": 0, "hostId": "", "hostSourceFrame": 0,
                                  "transform": transform(.4, .7, .7)}]
    if workload == "text":
        data["textOverlays"] = [{"text": "Workstation benchmark", "startFrame": 0,
                                 "durationFrames": frames, "fontSize": 72, "fontFamily": "Arial", "style": "clean",
                                 "color": "#ffffff", "bold": True, "shadow": True, "fade": False, "x": .5, "y": .2}]
    return data


def source_media(ffmpeg, destination, width, height, seconds, codec):
    codec_options = ["-c:v", "libx264", "-preset", "ultrafast", "-crf", "20", "-x264-params",
                     "colorprim=bt709:transfer=bt709:colormatrix=bt709"]
    if codec == "hevc":
        codec_options = ["-c:v", "libx265", "-preset", "ultrafast", "-crf", "20", "-tag:v", "hvc1",
                         "-x265-params", "pools=2:frame-threads=2:log-level=error:colorprim=bt709:transfer=bt709:colormatrix=bt709"]
    command([ffmpeg, "-nostdin", "-v", "error", "-filter_threads", "2", "-f", "lavfi", "-i",
             f"testsrc2=size={width}x{height}:rate=60:duration={seconds}", "-f", "lavfi", "-i",
             f"sine=frequency=440:sample_rate=48000:duration={seconds}", *codec_options, "-threads", "2",
             "-color_range", "tv", "-pix_fmt", "yuv420p", "-c:a", "aac", "-t", str(seconds), "-n", destination], timeout=300)


def export(editor, snapshot, video, software, iphone, seconds, legacy):
    flags = (["--software"] if software else []) + (["--iphone"] if iphone else [])
    arguments = [editor, "--benchmark-export", snapshot, video, *flags]
    if legacy:
        arguments = [editor, "--export-worker", snapshot, video, video.with_suffix(".cancel"), *flags]
    log_path = video.with_suffix(".log")
    started = time.perf_counter()
    with log_path.open("w", encoding="utf-8") as log, NvidiaSampler() as gpu:
        process = subprocess.Popen([str(x) for x in arguments], stdout=log, stderr=subprocess.STDOUT,
                                   env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "SDL_AUDIODRIVER": "dummy"})
        try:
            process.wait(timeout=max(120, seconds * 30))
        except subprocess.TimeoutExpired:
            if legacy:
                video.with_suffix(".cancel").touch()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
            if process.poll() is None:
                process.kill()
                process.wait()
            raise RuntimeError(f"Export timed out; see {log_path}")
        elapsed = time.perf_counter() - started
    messages = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            event = json.loads(line)
            if event.get("event") == "diagnostic":
                messages.append(event["message"])
        except (ValueError, AttributeError, KeyError):
            pass
    # Older builds lack the supervised command. Preserve their hardware-crash retry
    # when comparing the identical worker protocol on both revisions.
    if legacy and not software and (process.returncode < 0 or process.returncode >= 0xC0000000):
        retry = export(editor, snapshot, video, True, iphone, seconds, legacy)
        retry["elapsedSeconds"] = time.perf_counter() - started
        retry["diagnostics"] = messages + ["Hardware worker crashed; software retry"] + retry["diagnostics"]
        return retry
    if process.returncode or not video.is_file():
        raise RuntimeError(f"Export failed; see {log_path}")
    stages, metrics = [], {}
    for message in messages:
        if message.startswith("Export stage: "):
            stages.append(json.loads(message[len("Export stage: "):]))
        elif message.startswith("Export metrics: "):
            metrics = json.loads(message[len("Export metrics: "):])
    return {"elapsedSeconds": elapsed, "metrics": metrics, "stages": stages, "nvidiaUtilization": gpu.report(elapsed),
            "diagnostics": messages, "video": str(video), "protocol": "worker" if legacy else "supervised ExportJob"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--baseline-editor", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="New local results folder")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--matrix", action="store_true", help="H.264/HEVC at 1080p and 4K")
    parser.add_argument("--iphone", action="store_true")
    parser.add_argument("--visible", action="store_true", help="Show the editing benchmark window to include real presentation")
    parser.add_argument("--workloads", choices=("plain", "overlay", "text"), nargs="+", default=["plain", "overlay", "text"])
    args = parser.parse_args()
    if not 1 <= args.repeat <= 10 or not .5 <= args.seconds <= 3600:
        parser.error("Use repeat 1–10 and seconds 0.5–3600")
    editors = [("current", args.editor.resolve())]
    if args.baseline_editor:
        editors.insert(0, ("baseline", args.baseline_editor.resolve()))
    if any(not editor.is_file() for _, editor in editors):
        parser.error("Editor must be the executable inside the app, not the .app directory")
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        parser.error("ffmpeg and ffprobe must be installed")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    seconds = round(args.seconds * 60) / 60
    report = {"schemaVersion": 1, "platform": platform.platform(), "cpuCount": os.cpu_count(), "seconds": seconds,
              "binaries": {name: {"path": str(editor), "sha256": hashlib.sha256(editor.read_bytes()).hexdigest()} for name, editor in editors},
              "outputStorage": {"path": str(args.output), "freeBytes": shutil.disk_usage(args.output).free},
              "ffmpegVersion": command([ffmpeg, "-version"], capture_output=True, text=True).stdout.splitlines()[0],
              "samples": [], "editing": [], "notes": ["Elapsed export time includes startup and internal validation; independent decode validation runs afterward.",
              "Compare actual pipeline/encoder fields, not requested auto/software mode. Missing metrics in older builds stay unavailable.",
              "Worker RSS excludes FFmpeg children. A short local test does not establish long-run stability or NVIDIA performance."]}
    smi = shutil.which("nvidia-smi")
    if smi:
        gpu = subprocess.run([smi, "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"], capture_output=True, text=True, timeout=10)
        report["nvidia"] = {"returncode": gpu.returncode, "description": gpu.stdout.strip(), "error": gpu.stderr.strip()}
    matrix = [(1920, 1080, "h264")]
    if args.matrix:
        matrix = [(width, height, codec) for width, height in ((1920, 1080), (3840, 2160)) for codec in ("h264", "hevc")]
    for width, height, codec in matrix:
        source = args.output / f"source-{height}-{codec}.mp4"
        source_media(ffmpeg, source, width, height, seconds, codec)
        for workload in args.workloads:
            case = f"{height}-{codec}-{workload}"
            snapshot = args.output / f"{case}.isaac.json"
            snapshot.write_text(json.dumps(project(source, width, height, seconds, workload)), encoding="utf-8")
            for repeat in range(args.repeat):
                # Alternate revision order to reduce systematic warm-up bias.
                for name, editor in (editors if repeat % 2 == 0 else list(reversed(editors))):
                    for software in (True, False):
                        video = args.output / f"{case}-{name}-{repeat}-{'software' if software else 'auto'}.mp4"
                        sample = export(editor, snapshot, video, software, args.iphone, seconds, bool(args.baseline_editor))
                        probe = command([ffprobe, "-v", "error", "-count_frames", "-show_streams", "-of", "json", video], capture_output=True, text=True, timeout=max(60, seconds * 10))
                        streams = json.loads(probe.stdout)["streams"]
                        video_stream = next(item for item in streams if item["codec_type"] == "video")
                        if int(video_stream["nb_read_frames"]) != round(seconds * 60):
                            raise RuntimeError(f"Incomplete frame count: {video}")
                        sample.update(case=case, revision=name, repeat=repeat, requestedMode="software" if software else "auto", streams=streams)
                        report["samples"].append(sample)
                        (args.output / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
                        print(case, name, sample["requestedMode"], f"{sample['elapsedSeconds']:.3f}s", sample["metrics"].get("pipeline", "unavailable"), flush=True)
            editing_path = args.output / f"{case}-editing.json"
            command([args.editor.resolve(), "--performance-report", snapshot, editing_path, *(["--visible"] if args.visible else [])], timeout=120)
            report["editing"].append({"case": case, "report": json.loads(editing_path.read_text())})
    report["medians"] = [{"case": case, "revision": name, "requestedMode": mode,
                           "seconds": statistics.median(item["elapsedSeconds"] for item in report["samples"] if (item["case"], item["revision"], item["requestedMode"]) == (case, name, mode))}
                          for case, name, mode in sorted({(item["case"], item["revision"], item["requestedMode"]) for item in report["samples"]})]
    (args.output / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("Saved", args.output / "results.json")


if __name__ == "__main__":
    main()
