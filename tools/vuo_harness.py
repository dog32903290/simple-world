#!/usr/bin/env python3
"""
Minimal agent harness for Vuo GUI debugging.

This wraps the real Vuo app. It does not parse or emulate Vuo compositions.
Commands print JSON so Codex/Hermes can inspect state without scraping prose.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import zlib
from datetime import datetime
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
VUO_APP = REPO / "external/vuo-downloads/vuo-2.4.6/Vuo.app"
VUO_SDK_ROOT = REPO / "external/vuo-downloads/vuo-sdk-local/framework"
VUO_COMPILE = VUO_SDK_ROOT / "vuo-compile"
VUO_LINK = VUO_SDK_ROOT / "vuo-link"
VUO_EXPORT = VUO_SDK_ROOT / "vuo-export"
ARTIFACT_ROOT = REPO / "artifacts/vuo_harness"
CLI_ARTIFACT_ROOT = REPO / "artifacts/vuo_cli"


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status")

    open_parser = sub.add_parser("open")
    open_parser.add_argument("composition")

    shot_parser = sub.add_parser("screenshot")
    shot_parser.add_argument("--name", default="")

    logs_parser = sub.add_parser("logs")
    logs_parser.add_argument("--minutes", type=int, default=10)

    sub.add_parser("run")
    sub.add_parser("stop")
    sub.add_parser("reset")

    sub.add_parser("cli-status")

    cli_build_parser = sub.add_parser("cli-build")
    cli_build_parser.add_argument("composition")
    cli_build_parser.add_argument("--name", default="")

    cli_run_parser = sub.add_parser("cli-run")
    cli_run_parser.add_argument("executable")
    cli_run_parser.add_argument("--seconds", type=float, default=3.0)
    cli_run_parser.add_argument("--name", default="")

    cli_proof_parser = sub.add_parser("cli-proof")
    cli_proof_parser.add_argument("composition")
    cli_proof_parser.add_argument("--seconds", type=float, default=3.0)
    cli_proof_parser.add_argument("--name", default="")

    args = parser.parse_args()

    try:
        if args.command == "status":
            return emit(status())
        if args.command == "open":
            return emit(open_composition(Path(args.composition)))
        if args.command == "screenshot":
            return emit(screenshot(args.name))
        if args.command == "logs":
            return emit(logs(args.minutes))
        if args.command == "run":
            return emit(send_shortcut("r", ["command"], "run"))
        if args.command == "stop":
            return emit(send_shortcut(".", ["command"], "stop"))
        if args.command == "reset":
            return emit(reset_vuo())
        if args.command == "cli-status":
            return emit(cli_status())
        if args.command == "cli-build":
            return emit(cli_build(Path(args.composition), args.name))
        if args.command == "cli-run":
            return emit(cli_run_executable(Path(args.executable), args.seconds, args.name))
        if args.command == "cli-proof":
            build = cli_build(Path(args.composition), args.name)
            if not build.get("ok"):
                return emit({"ok": False, "stage": "build", "build": build})
            run_result = cli_run_executable(Path(build["executable"]), args.seconds, args.name or Path(args.composition).stem)
            return emit({"ok": build.get("ok") and run_result.get("ok"), "build": build, "run": run_result})
    except subprocess.CalledProcessError as exc:
        return emit({
            "ok": False,
            "error": "command_failed",
            "command": exc.cmd,
            "returncode": exc.returncode,
            "stdout": exc.stdout,
            "stderr": exc.stderr,
        }, code=1)

    return emit({"ok": False, "error": "unknown_command"}, code=2)


def status() -> dict:
    processes = run(["pgrep", "-fl", "Vuo"], check=False).stdout.splitlines()
    return {
        "ok": True,
        "vuoApp": str(VUO_APP),
        "vuoAppExists": VUO_APP.exists(),
        "vuoSdk": cli_status(),
        "processes": processes,
        "windows": get_vuo_windows(),
        "artifactRoot": str(ARTIFACT_ROOT),
    }


def reset_vuo() -> dict:
    before = status()["processes"]
    for process in before:
        if "/Vuo.app/Contents/MacOS/Vuo" not in process and "VuoComposition-" not in process:
            continue
        pid = process.split(" ", 1)[0]
        run(["kill", pid], check=False)
    time.sleep(1)
    after = status()["processes"]
    return {
        "ok": not any("/Vuo.app/Contents/MacOS/Vuo" in line or "VuoComposition-" in line for line in after),
        "action": "reset",
        "processesBefore": before,
        "processesAfter": after,
    }


def open_composition(composition: Path) -> dict:
    composition = composition.expanduser().resolve()
    if not composition.exists():
        return {"ok": False, "error": "composition_not_found", "composition": str(composition)}

    before = get_vuo_windows()
    result = run(["open", "-a", str(VUO_APP), str(composition)], check=False)
    if result.returncode != 0:
        return {
            "ok": False,
            "error": "open_failed",
            "composition": str(composition),
            "stderr": result.stderr,
            "stdout": result.stdout,
        }

    windows = wait_for_vuo_window(composition.stem, timeout=8)
    if not windows:
        return {
            "ok": False,
            "error": "composition_window_not_found",
            "composition": str(composition),
            "windowsBefore": before,
            "windowsAfter": get_vuo_windows(),
            "hint": "macOS accepted the open request, but Vuo did not expose a document window. Open the composition manually or reset Vuo/LaunchServices before visual verification.",
        }

    return {"ok": True, "composition": str(composition), "windows": windows}


def screenshot(name: str) -> dict:
    ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    safe_name = name or f"vuo_{stamp}"
    path = ARTIFACT_ROOT / f"{safe_name}.png"
    result = run(["screencapture", "-x", str(path)], check=False)
    if result.returncode != 0:
        return {
            "ok": False,
            "error": "screenshot_failed",
            "path": str(path),
            "stderr": result.stderr,
            "hint": "macOS may require Screen Recording permission for the terminal/Codex app.",
        }
    return {
        "ok": path.exists() and path.stat().st_size > 0,
        "path": str(path),
        "bytes": path.stat().st_size if path.exists() else 0,
    }


def logs(minutes: int) -> dict:
    ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    path = ARTIFACT_ROOT / "vuo_recent.log"
    predicate = (
        'process CONTAINS "Vuo" AND '
        '(eventMessage CONTAINS "my.field.render" OR '
        'eventMessage CONTAINS "my.field.generate" OR '
        'eventMessage CONTAINS "myworld.shader" OR '
        'eventMessage CONTAINS "raymarchField" OR '
        'eventMessage CONTAINS "sphereSdf" OR '
        'eventMessage CONTAINS "Failed to compile" OR '
        'eventMessage CONTAINS "GLSL" OR '
        'eventMessage CONTAINS "OpenGL" OR '
        'eventMessage CONTAINS "Metal")'
    )
    result = run([
        "/usr/bin/log",
        "show",
        "--last",
        f"{minutes}m",
        "--style",
        "compact",
        "--predicate",
        predicate,
    ], check=False)
    path.write_text(result.stdout, encoding="utf8")
    return {
        "ok": result.returncode == 0,
        "path": str(path),
        "bytes": path.stat().st_size,
        "returncode": result.returncode,
        "stderr": result.stderr,
    }


def cli_status() -> dict:
    tools = {
        "vuoCompile": VUO_COMPILE,
        "vuoLink": VUO_LINK,
        "vuoExport": VUO_EXPORT,
    }
    tool_status = {
        name: {
            "path": str(path),
            "exists": path.exists(),
            "executable": os.access(path, os.X_OK),
        }
        for name, path in tools.items()
    }
    return {
        "ok": all(item["exists"] and item["executable"] for item in tool_status.values()),
        "sdkRoot": str(VUO_SDK_ROOT),
        "tools": tool_status,
        "artifactRoot": str(CLI_ARTIFACT_ROOT),
    }


def cli_build(composition: Path, name: str = "") -> dict:
    sdk = cli_status()
    if not sdk["ok"]:
        return {
            "ok": False,
            "error": "vuo_sdk_missing",
            "sdk": sdk,
            "hint": "Extract or install the Vuo SDK so vuo-compile and vuo-link are available.",
        }

    composition = composition.expanduser().resolve()
    if not composition.exists():
        return {"ok": False, "error": "composition_not_found", "composition": str(composition)}

    reset = reset_vuo()
    CLI_ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    stem = safe_artifact_name(name or composition.stem)
    bitcode = CLI_ARTIFACT_ROOT / f"{stem}.bc"
    executable = CLI_ARTIFACT_ROOT / stem

    compile_result = run_and_log(
        [str(VUO_COMPILE), "--output", str(bitcode), str(composition)],
        CLI_ARTIFACT_ROOT / f"{stem}.compile.stdout.log",
        CLI_ARTIFACT_ROOT / f"{stem}.compile.stderr.log",
    )
    if compile_result["returncode"] != 0 or not bitcode.exists():
        return {
            "ok": False,
            "stage": "compile",
            "error": "vuo_compile_failed",
            "composition": str(composition),
            "bitcode": str(bitcode),
            "reset": reset,
            "compile": compile_result,
        }

    link_result = run_and_log(
        [str(VUO_LINK), "--output", str(executable), str(bitcode)],
        CLI_ARTIFACT_ROOT / f"{stem}.link.stdout.log",
        CLI_ARTIFACT_ROOT / f"{stem}.link.stderr.log",
    )
    if link_result["returncode"] != 0 or not executable.exists():
        return {
            "ok": False,
            "stage": "link",
            "error": "vuo_link_failed",
            "composition": str(composition),
            "bitcode": str(bitcode),
            "executable": str(executable),
            "reset": reset,
            "compile": compile_result,
            "link": link_result,
        }

    log_text = read_log_text(compile_result) + "\n" + read_log_text(link_result)
    return {
        "ok": True,
        "composition": str(composition),
        "bitcode": str(bitcode),
        "bitcodeBytes": bitcode.stat().st_size,
        "executable": str(executable),
        "executableBytes": executable.stat().st_size,
        "reset": reset,
        "compile": compile_result,
        "link": link_result,
        "loadedUserNodes": extract_loaded_user_nodes(log_text),
        "warnings": extract_warning_lines(log_text),
    }


def cli_run_executable(executable: Path, seconds: float, name: str = "") -> dict:
    executable = executable.expanduser().resolve()
    if not executable.exists():
        return {"ok": False, "error": "executable_not_found", "executable": str(executable)}
    if not os.access(executable, os.X_OK):
        return {"ok": False, "error": "executable_not_executable", "executable": str(executable)}

    CLI_ARTIFACT_ROOT.mkdir(parents=True, exist_ok=True)
    stem = safe_artifact_name(name or executable.stem)
    stdout_path = CLI_ARTIFACT_ROOT / f"{stem}.run.stdout.log"
    stderr_path = CLI_ARTIFACT_ROOT / f"{stem}.run.stderr.log"
    screenshot_path = CLI_ARTIFACT_ROOT / f"{stem}.run.png"

    with stdout_path.open("w", encoding="utf8") as stdout_file, stderr_path.open("w", encoding="utf8") as stderr_file:
        process = subprocess.Popen(
            [str(executable)],
            stdout=stdout_file,
            stderr=stderr_file,
            text=True,
            cwd=str(REPO),
        )
        time.sleep(max(0.1, seconds))
        shot = capture_runner_window_png(process.pid, executable.stem, screenshot_path, timeout=max(1.0, seconds))
        still_running = process.poll() is None
        if still_running:
            stop_result = stop_process(process)
        else:
            stop_result = {"ok": True, "method": "already_exited"}
        returncode = process.returncode

    stdout_text = stdout_path.read_text(encoding="utf8", errors="replace")
    stderr_text = stderr_path.read_text(encoding="utf8", errors="replace")
    evidence = extract_runtime_evidence(stdout_text + "\n" + stderr_text)
    visual = shot.get("imageInfo", {}).get("visual", {})
    visual_ok = not visual.get("mostlyBlack", False)
    error = "" if visual_ok else "visual_output_mostly_black"
    return {
        "ok": shot.get("ok", False) and evidence["startedBackend"] and visual_ok,
        "error": error,
        "executable": str(executable),
        "seconds": seconds,
        "pid": process.pid,
        "returncodeAfterStop": returncode,
        "stop": stop_result,
        "stdout": str(stdout_path),
        "stdoutBytes": stdout_path.stat().st_size,
        "stderr": str(stderr_path),
        "stderrBytes": stderr_path.stat().st_size,
        "screenshot": shot,
        "runtimeEvidence": evidence,
        "warnings": extract_warning_lines(stdout_text + "\n" + stderr_text),
    }


def send_shortcut(key: str, modifiers: list[str], action: str) -> dict:
    before = status()["processes"]
    modifier_text = " down, ".join(modifiers) + " down" if modifiers else ""
    script = f'''
tell application "Vuo" to activate
delay 0.2
tell application "System Events"
  keystroke "{key}" using {{{modifier_text}}}
end tell
'''
    result = run(["osascript", "-e", script], check=False)
    if result.returncode != 0:
        return {
            "ok": False,
            "action": action,
            "error": "shortcut_failed",
            "stderr": result.stderr,
            "hint": "Enable Accessibility permission for the terminal/Codex app if System Events is denied.",
        }

    if action == "run":
        runner = wait_for_composition_runner(timeout=6)
        if not runner:
            return {
                "ok": False,
                "action": action,
                "error": "composition_runner_not_found",
                "processesBefore": before,
                "processesAfter": status()["processes"],
                "windows": get_vuo_windows(),
                "hint": "The run shortcut was sent, but no VuoComposition runner appeared. The composition may not be open/focused.",
            }
        return {"ok": True, "action": action, "runner": runner}

    return {"ok": True, "action": action, "processesBefore": before, "processesAfter": status()["processes"]}


def stop_process(process: subprocess.Popen) -> dict:
    process.terminate()
    try:
        process.wait(timeout=2)
        return {"ok": True, "method": "terminate", "returncode": process.returncode}
    except subprocess.TimeoutExpired:
        process.kill()
    try:
        process.wait(timeout=2)
        return {"ok": True, "method": "kill", "returncode": process.returncode}
    except subprocess.TimeoutExpired:
        return {
            "ok": False,
            "error": "runner_stop_timeout",
            "pid": process.pid,
            "hint": "The Vuo executable did not exit after SIGTERM/SIGKILL; inspect the process state and restart Vuo/macOS if it remains stuck.",
        }


def get_vuo_windows() -> list[str]:
    script = 'tell application "System Events" to tell process "Vuo" to get name of every window'
    result = run(["osascript", "-e", script], check=False)
    if result.returncode != 0:
        return []
    return [name.strip() for name in result.stdout.strip().split(",") if name.strip()]


def wait_for_vuo_window(expected: str, timeout: float) -> list[str]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        windows = get_vuo_windows()
        if windows and any(expected in window or window.endswith(".vuo") for window in windows):
            return windows
        time.sleep(0.25)
    return []


def wait_for_composition_runner(timeout: float) -> list[str]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        runners = [line for line in status()["processes"] if "VuoComposition-" in line]
        if runners:
            return runners
        time.sleep(0.25)
    return []


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, text=True, capture_output=True, check=check)


def run_and_log(cmd: list[str], stdout_path: Path, stderr_path: Path) -> dict:
    result = run(cmd, check=False)
    stdout_path.write_text(result.stdout, encoding="utf8")
    stderr_path.write_text(result.stderr, encoding="utf8")
    return {
        "command": cmd,
        "returncode": result.returncode,
        "stdout": str(stdout_path),
        "stdoutBytes": stdout_path.stat().st_size,
        "stderr": str(stderr_path),
        "stderrBytes": stderr_path.stat().st_size,
    }


def capture_runner_window_png(pid: int, expected_name: str, path: Path, timeout: float) -> dict:
    window = wait_for_runner_window(pid, expected_name, timeout)
    if not window:
        return {
            "ok": False,
            "error": "runner_window_not_found",
            "path": str(path),
            "pid": pid,
            "expectedName": expected_name,
            "hint": "The executable started, but no named render window was visible to CoreGraphics.",
        }
    shot = capture_png(path, window["id"])
    shot["window"] = window
    return shot


def wait_for_runner_window(pid: int, expected_name: str, timeout: float) -> dict:
    deadline = time.time() + timeout
    best = {}
    while time.time() < deadline:
        windows = get_cg_windows_for_pid(pid)
        visible = [window for window in windows if window.get("width", 0) > 0 and window.get("height", 0) > 0]
        named = [window for window in visible if window.get("name")]
        matching = [window for window in named if expected_name in window.get("name", "")]
        if matching:
            return max(matching, key=lambda window: window["width"] * window["height"])
        if named:
            best = max(named, key=lambda window: window["width"] * window["height"])
        elif visible and not best:
            best = max(visible, key=lambda window: window["width"] * window["height"])
        time.sleep(0.25)
    return best


def get_cg_windows_for_pid(pid: int) -> list[dict]:
    script = r'''
import CoreGraphics
import Foundation

let pid = Int32(CommandLine.arguments[1])!
let windows = CGWindowListCopyWindowInfo(CGWindowListOption(arrayLiteral: .optionAll), kCGNullWindowID) as! [[String: Any]]
var rows: [[String: Any]] = []

for window in windows {
    guard let ownerPid = window[kCGWindowOwnerPID as String] as? Int32, ownerPid == pid else { continue }
    guard let number = window[kCGWindowNumber as String] as? Int else { continue }
    let name = window[kCGWindowName as String] as? String ?? ""
    let owner = window[kCGWindowOwnerName as String] as? String ?? ""
    let onscreen = window[kCGWindowIsOnscreen as String] as? Int ?? 0
    let bounds = window[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let x = bounds["X"] as? Int ?? 0
    let y = bounds["Y"] as? Int ?? 0
    let width = bounds["Width"] as? Int ?? 0
    let height = bounds["Height"] as? Int ?? 0
    rows.append(["id": number, "name": name, "owner": owner, "onscreen": onscreen, "x": x, "y": y, "width": width, "height": height])
}

let json = try! JSONSerialization.data(withJSONObject: rows)
print(String(data: json, encoding: .utf8)!)
'''
    result = run(["swift", "-e", script, str(pid)], check=False)
    if result.returncode != 0:
        return []
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return []


def capture_png(path: Path, window_id: int | None = None) -> dict:
    cmd = ["screencapture", "-x"]
    if window_id is not None:
        cmd.extend(["-l", str(window_id)])
    cmd.append(str(path))
    result = run(cmd, check=False)
    payload = {
        "ok": result.returncode == 0 and path.exists() and path.stat().st_size > 0,
        "path": str(path),
        "bytes": path.stat().st_size if path.exists() else 0,
        "returncode": result.returncode,
        "windowId": window_id,
    }
    if result.returncode != 0:
        payload["error"] = "screenshot_failed"
        payload["stderr"] = result.stderr
        payload["hint"] = "macOS may require Screen Recording permission for the terminal/Codex app."
    else:
        payload["imageInfo"] = image_info(path)
    return payload


def image_info(path: Path) -> dict:
    result = run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", "-g", "format", str(path)], check=False)
    info = {"ok": result.returncode == 0}
    for line in result.stdout.splitlines():
        line = line.strip()
        if ":" not in line:
            continue
        key, value = [part.strip() for part in line.split(":", 1)]
        if key in {"pixelWidth", "pixelHeight"}:
            try:
                info[key] = int(value)
            except ValueError:
                info[key] = value
        if key == "format":
            info[key] = value
    info["visual"] = png_visual_info(path)
    return info


def png_visual_info(path: Path) -> dict:
    try:
        pixels = decode_png_pixels(path)
    except Exception as exc:  # noqa: BLE001 - artifact inspection should report, not crash the harness.
        return {"ok": False, "error": "png_decode_failed", "detail": str(exc)}
    if not pixels.get("pixels"):
        return {"ok": False, "error": "png_no_pixels"}

    content_pixels = crop_content_pixels(pixels)
    sample_count, average_luma, bright_ratio = summarize_luma(content_pixels)
    full_sample_count, full_average_luma, full_bright_ratio = summarize_luma(pixels["pixels"])
    return {
        "ok": True,
        "sampleCount": sample_count,
        "averageLuma": round(average_luma, 6),
        "brightRatio": round(bright_ratio, 6),
        "mostlyBlack": average_luma < 0.01 and bright_ratio < 0.001,
        "contentCrop": {"left": 0.05, "top": 0.12, "right": 0.95, "bottom": 0.95},
        "fullWindow": {
            "sampleCount": full_sample_count,
            "averageLuma": round(full_average_luma, 6),
            "brightRatio": round(full_bright_ratio, 6),
            "mostlyBlack": full_average_luma < 0.01 and full_bright_ratio < 0.001,
        },
    }


def summarize_luma(pixels: list[tuple[int, int, int, int]]) -> tuple[int, float, float]:
    sample_count = 0
    luma_total = 0.0
    bright_count = 0
    stride = max(1, len(pixels) // 200000)
    for red, green, blue, alpha in pixels[::stride]:
        if alpha == 0:
            continue
        luma = (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0
        luma_total += luma
        bright_count += 1 if luma > 0.03 else 0
        sample_count += 1

    average_luma = luma_total / sample_count if sample_count else 0.0
    bright_ratio = bright_count / sample_count if sample_count else 0.0
    return sample_count, average_luma, bright_ratio


def crop_content_pixels(image: dict) -> list[tuple[int, int, int, int]]:
    width = image["width"]
    height = image["height"]
    pixels = image["pixels"]
    left = int(width * 0.05)
    right = int(width * 0.95)
    top = int(height * 0.12)
    bottom = int(height * 0.95)
    cropped = []
    for y in range(top, bottom):
        start = y * width + left
        cropped.extend(pixels[start:y * width + right])
    return cropped


def decode_png_pixels(path: Path) -> dict:
    data = path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("not a png")

    position = 8
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()
    while position < len(data):
        length = int.from_bytes(data[position:position + 4], "big")
        kind = data[position + 4:position + 8]
        chunk = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width = int.from_bytes(chunk[0:4], "big")
            height = int.from_bytes(chunk[4:8], "big")
            bit_depth = chunk[8]
            color_type = chunk[9]
            interlace = chunk[12]
        elif kind == b"IDAT":
            idat.extend(chunk)
        elif kind == b"IEND":
            break

    if width is None or height is None or bit_depth != 8 or interlace != 0:
        raise ValueError("unsupported png layout")

    channels_by_type = {0: 1, 2: 3, 4: 2, 6: 4}
    if color_type not in channels_by_type:
        raise ValueError(f"unsupported png color type {color_type}")

    channels = channels_by_type[color_type]
    row_bytes = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    offset = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset:offset + row_bytes])
        offset += row_bytes
        recon = unfilter_png_row(filter_type, row, previous, channels)
        rows.append(recon)
        previous = recon

    pixels = []
    for row in rows:
        for index in range(0, len(row), channels):
            if color_type == 0:
                gray = row[index]
                pixels.append((gray, gray, gray, 255))
            elif color_type == 2:
                pixels.append((row[index], row[index + 1], row[index + 2], 255))
            elif color_type == 4:
                gray = row[index]
                pixels.append((gray, gray, gray, row[index + 1]))
            elif color_type == 6:
                pixels.append((row[index], row[index + 1], row[index + 2], row[index + 3]))
    return {"width": width, "height": height, "pixels": pixels}


def unfilter_png_row(filter_type: int, row: bytearray, previous: bytearray, bytes_per_pixel: int) -> bytearray:
    recon = bytearray(len(row))
    for index, value in enumerate(row):
        left = recon[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
        up = previous[index]
        up_left = previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
        if filter_type == 0:
            recon[index] = value
        elif filter_type == 1:
            recon[index] = (value + left) & 0xFF
        elif filter_type == 2:
            recon[index] = (value + up) & 0xFF
        elif filter_type == 3:
            recon[index] = (value + ((left + up) // 2)) & 0xFF
        elif filter_type == 4:
            recon[index] = (value + paeth(left, up, up_left)) & 0xFF
        else:
            raise ValueError(f"unsupported png filter {filter_type}")
    return recon


def paeth(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    left_distance = abs(estimate - left)
    up_distance = abs(estimate - up)
    up_left_distance = abs(estimate - up_left)
    if left_distance <= up_distance and left_distance <= up_left_distance:
        return left
    if up_distance <= up_left_distance:
        return up
    return up_left


def read_log_text(result: dict) -> str:
    texts = []
    for key in ("stdout", "stderr"):
        path = Path(result[key])
        if path.exists():
            texts.append(path.read_text(encoding="utf8", errors="replace"))
    return "\n".join(texts)


def extract_loaded_user_nodes(text: str) -> list[str]:
    nodes = []
    for line in text.splitlines():
        line = strip_ansi(line)
        if "Loaded into user environment" not in line:
            continue
        match = re.search(r"Loaded into user environment:\s+\[[0-9a-fA-F]+\]\s+([A-Za-z0-9_.]+)\s+\(", line)
        node = match.group(1) if match else ""
        if node and node not in nodes:
            nodes.append(node)
    return nodes


def extract_warning_lines(text: str) -> list[str]:
    warnings = []
    for line in text.splitlines():
        if "warning" in line.lower() or "unsupported" in line.lower() or "No Vuo Pro license" in line:
            cleaned = line.strip()
            if cleaned and cleaned not in warnings:
                warnings.append(cleaned)
    return warnings[:40]


def extract_runtime_evidence(text: str) -> dict:
    return {
        "startedBackend": any(token in text for token in ["Created OpenGL context", "VuoGlContext_renderers()", "VuoMetal"]),
        "createdOpenGlContext": "Created OpenGL context" in text,
        "sawOpenGlRenderer": "VuoGlContext_renderers()" in text or "OpenGL" in text,
        "sawMetalDevice": "VuoMetal" in text or "Metal device" in text,
        "sawSceneRenderer": "VuoSceneRenderer_renderInternal" in text,
        "sawTextureWarning": "GLD_TEXTURE_INDEX_2D is unloadable" in text,
    }


def safe_artifact_name(name: str) -> str:
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "-" for ch in name.strip())
    return safe or datetime.now().strftime("vuo_cli_%Y%m%d_%H%M%S")


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def emit(payload: dict, code: int | None = None) -> int:
    print(json.dumps(payload, indent=2, ensure_ascii=False))
    if code is not None:
        return code
    return 0 if payload.get("ok", False) else 1


if __name__ == "__main__":
    raise SystemExit(main())
