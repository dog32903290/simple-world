#!/usr/bin/env python3
import argparse
import base64
import json
import socket
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


PROTOCOL = "bespoke-cli/v0"
ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / "logs" / "bespoke-cli"
LOG_PATH = LOG_DIR / "run.jsonl"
DEFAULT_LIVE_HOST = "127.0.0.1"
DEFAULT_LIVE_PORT = 9001
DEFAULT_STATUS_PATH = "/tmp/bespoke_agent_bridge_status.json"


class BespokeCliError(Exception):
    def __init__(self, error_type, message, repair_hint, context=None, retryable=False):
        super().__init__(message)
        self.error_type = error_type
        self.message = message
        self.repair_hint = repair_hint
        self.context = context or {}
        self.retryable = retryable


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def log_response(response):
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    with LOG_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps({"timestamp": now_iso(), **response}, ensure_ascii=False) + "\n")


def base_response(command):
    return {
        "ok": True,
        "protocol": PROTOCOL,
        "command": command,
        "logs": {"run_log": str(LOG_PATH)},
    }


def error_response(error, command):
    return {
        "ok": False,
        "protocol": PROTOCOL,
        "command": command,
        "error": {
            "type": error.error_type,
            "message": error.message,
            "repair_hint": error.repair_hint,
            "retryable": error.retryable,
        },
        "context": error.context,
        "logs": {"run_log": str(LOG_PATH)},
    }


def read_bsk(path):
    path = Path(path)
    if not path.exists():
        raise BespokeCliError(
            "file_not_found",
            f"Patch file not found: {path}",
            "Pass an existing .bsk file path.",
            {"path": str(path)},
        )

    raw = path.read_bytes()
    if len(raw) < 8:
        raise BespokeCliError(
            "invalid_bsk",
            "Bespoke patch is shorter than the 8-byte header.",
            "Use a real Bespoke .bsk file.",
            {"path": str(path), "bytes": len(raw)},
        )

    json_len = int.from_bytes(raw[:4], "little")
    json_start = 8
    json_end = json_start + json_len
    if json_len <= 0 or json_end > len(raw):
        raise BespokeCliError(
            "invalid_bsk_header",
            "Bespoke JSON length header is outside the file bounds.",
            "Check that the file is a current Bespoke .bsk save.",
            {"path": str(path), "json_len": json_len, "bytes": len(raw)},
        )

    try:
        patch = json.loads(raw[json_start:json_end].decode("utf-8"))
    except UnicodeDecodeError as exc:
        raise BespokeCliError(
            "invalid_bsk_json_encoding",
            "Bespoke JSON section is not valid UTF-8.",
            "Open and re-save the patch in Bespoke, then retry.",
            {"path": str(path), "offset": json_start},
        ) from exc
    except json.JSONDecodeError as exc:
        raise BespokeCliError(
            "invalid_bsk_json",
            "Bespoke JSON section could not be parsed.",
            "Open and re-save the patch in Bespoke, then retry.",
            {"path": str(path), "line": exc.lineno, "column": exc.colno},
        ) from exc

    if not isinstance(patch, dict) or not isinstance(patch.get("modules"), list):
        raise BespokeCliError(
            "invalid_bsk_schema",
            "Patch JSON does not contain a modules list.",
            "Use a Bespoke patch save with a top-level modules array.",
            {"path": str(path)},
        )

    return {
        "path": path,
        "raw": raw,
        "json_len": json_len,
        "patch": patch,
        "tail": raw[json_end:],
    }


def write_bsk(path, patch, tail):
    payload = json.dumps(patch, indent=3, ensure_ascii=False).encode("utf-8")
    Path(path).write_bytes(len(payload).to_bytes(4, "little") + (0).to_bytes(4, "little") + payload + tail)
    return len(payload)


def find_module(patch, name):
    for module in patch["modules"]:
        if module.get("name") == name:
            return module
    raise BespokeCliError(
        "module_not_found",
        f"Module not found: {name}",
        "Run `bespoke-cli list modules <patch.bsk> --json` and use an existing module name.",
        {"module": name},
    )


def module_summary(module):
    return {
        "name": module.get("name", ""),
        "type": module.get("type", ""),
        "position": module.get("position"),
        "connection_count": len(module.get("connections", [])) if isinstance(module.get("connections", []), list) else 0,
    }


def scalar_controls(module):
    skip = {"name", "type", "position", "connections"}
    controls = []
    name = module.get("name", "")
    for key in sorted(module.keys()):
        value = module[key]
        if key in skip:
            continue
        if isinstance(value, (str, int, float, bool)) or value is None:
            controls.append({"path": f"{name}.{key}", "value": value})
    return controls


def parse_value(value):
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if lowered == "null":
        return None
    try:
        return int(value)
    except ValueError:
        pass
    try:
        return float(value)
    except ValueError:
        return value


def osc_string(value):
    raw = value.encode("utf-8") + b"\0"
    padding = (4 - (len(raw) % 4)) % 4
    return raw + (b"\0" * padding)


def osc_message(address, string_arg):
    return osc_string(address) + osc_string(",s") + osc_string(string_arg)


def send_live_command(args, payload):
    status_path = Path(payload.get("status_path", DEFAULT_STATUS_PATH))
    if getattr(args, "wait", False) and status_path.exists():
        status_path.unlink()

    encoded = base64.b64encode(json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode("utf-8")).decode("ascii")
    packet = osc_message("/bespoke-agent/json", encoded)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(packet, (args.host, args.port))
    response = base_response("live " + args.live_command)
    response["state"] = {
        "host": args.host,
        "port": args.port,
        "packet_bytes": len(packet),
        "sent": True,
    }
    response["live_command"] = payload
    if getattr(args, "wait", False):
        response["bridge_result"] = wait_for_bridge_result(status_path, args.timeout)
    return response


def wait_for_bridge_result(status_path, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if status_path.exists():
            try:
                return json.loads(status_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                raise BespokeCliError(
                    "bridge_status_not_json",
                    "Bespoke bridge wrote a status file, but it is not valid JSON.",
                    "Inspect the bridge status file and Bespoke logs.",
                    {"status_path": str(status_path), "line": exc.lineno, "column": exc.colno},
                    retryable=True,
                ) from exc
        time.sleep(0.05)
    raise BespokeCliError(
        "bridge_ack_timeout",
        "Bespoke bridge did not write an ACK before the timeout.",
        "Make sure /Applications/BespokeSynth.app is running and the native bridge is listening on the requested OSC port.",
        {"status_path": str(status_path), "timeout_seconds": timeout_seconds},
        retryable=True,
    )


def command_status(args):
    bsk = read_bsk(args.patch)
    response = base_response("status")
    response["state"] = {
        "patch_path": str(bsk["path"]),
        "json_bytes": bsk["json_len"],
        "binary_tail_bytes": len(bsk["tail"]),
        "module_count": len(bsk["patch"]["modules"]),
        "modified": False,
    }
    return response


def command_list_modules(args):
    bsk = read_bsk(args.patch)
    response = base_response("list modules")
    response["state"] = {"patch_path": str(bsk["path"]), "modified": False}
    response["modules"] = [module_summary(module) for module in bsk["patch"]["modules"]]
    return response


def command_list_controls(args):
    bsk = read_bsk(args.patch)
    module = find_module(bsk["patch"], args.module)
    response = base_response("list controls")
    response["state"] = {"patch_path": str(bsk["path"]), "modified": False}
    response["module"] = module_summary(module)
    response["controls"] = scalar_controls(module)
    response["connections"] = module.get("connections", []) if isinstance(module.get("connections", []), list) else []
    return response


def command_validate(args):
    bsk = read_bsk(args.patch)
    response = base_response("validate")
    response["state"] = {
        "patch_path": str(bsk["path"]),
        "module_count": len(bsk["patch"]["modules"]),
        "json_bytes": bsk["json_len"],
        "binary_tail_bytes": len(bsk["tail"]),
        "verified": True,
    }
    return response


def command_set(args):
    bsk = read_bsk(args.patch)
    if "." not in args.path:
        raise BespokeCliError(
            "invalid_control_path",
            f"Control path must look like module.field: {args.path}",
            "Use a path returned by `list controls`.",
            {"path": args.path},
        )
    module_name, field = args.path.split(".", 1)
    module = find_module(bsk["patch"], module_name)
    if field not in module:
        raise BespokeCliError(
            "control_not_found",
            f"Control field not found: {args.path}",
            "Run `list controls` for the module and use an existing scalar field.",
            {"path": args.path, "module": module_name, "field": field},
        )
    old_value = module[field]
    new_value = parse_value(args.value)
    module[field] = new_value
    json_bytes = write_bsk(args.out, bsk["patch"], bsk["tail"])

    response = base_response("set")
    response["state"] = {
        "patch_path": str(bsk["path"]),
        "out_path": str(Path(args.out)),
        "modified": True,
        "json_bytes": json_bytes,
        "binary_tail_bytes": len(bsk["tail"]),
    }
    response["mutation"] = {
        "path": args.path,
        "old_value": old_value,
        "new_value": new_value,
    }
    return response


def command_live_status(args):
    payload = {
        "op": "status",
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def command_live_set(args):
    payload = {
        "op": "set",
        "path": args.path,
        "value": parse_value(args.value),
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def command_live_get(args):
    payload = {
        "op": "get",
        "path": args.path,
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def command_live_list_controls(args):
    payload = {
        "op": "list_controls",
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def command_live_list_sources(args):
    payload = {
        "op": "list_sources",
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def command_live_connect(args):
    payload = {
        "op": "connect",
        "source_module": args.source_module,
        "source_index": args.source_index,
        "target_path": args.target_path,
        "status_path": args.status_file,
    }
    return send_live_command(args, payload)


def add_live_common(parser):
    parser.add_argument("--host", default=DEFAULT_LIVE_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_LIVE_PORT)
    parser.add_argument("--status-file", default=DEFAULT_STATUS_PATH)
    parser.add_argument("--wait", action="store_true")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--json", action="store_true")


def build_parser():
    parser = argparse.ArgumentParser(prog="bespoke-cli")
    subparsers = parser.add_subparsers(dest="command", required=True)

    status = subparsers.add_parser("status")
    status.add_argument("patch")
    status.add_argument("--json", action="store_true")
    status.set_defaults(handler=command_status)

    list_parser = subparsers.add_parser("list")
    list_subparsers = list_parser.add_subparsers(dest="list_command", required=True)

    list_modules = list_subparsers.add_parser("modules")
    list_modules.add_argument("patch")
    list_modules.add_argument("--json", action="store_true")
    list_modules.set_defaults(handler=command_list_modules)

    list_controls = list_subparsers.add_parser("controls")
    list_controls.add_argument("patch")
    list_controls.add_argument("--module", required=True)
    list_controls.add_argument("--json", action="store_true")
    list_controls.set_defaults(handler=command_list_controls)

    validate = subparsers.add_parser("validate")
    validate.add_argument("patch")
    validate.add_argument("--json", action="store_true")
    validate.set_defaults(handler=command_validate)

    set_parser = subparsers.add_parser("set")
    set_parser.add_argument("patch")
    set_parser.add_argument("path")
    set_parser.add_argument("value")
    set_parser.add_argument("--out", required=True)
    set_parser.add_argument("--json", action="store_true")
    set_parser.set_defaults(handler=command_set)

    live = subparsers.add_parser("live")
    live_subparsers = live.add_subparsers(dest="live_command", required=True)

    live_status = live_subparsers.add_parser("status")
    add_live_common(live_status)
    live_status.set_defaults(handler=command_live_status)

    live_set = live_subparsers.add_parser("set")
    live_set.add_argument("path")
    live_set.add_argument("value")
    add_live_common(live_set)
    live_set.set_defaults(handler=command_live_set)

    live_get = live_subparsers.add_parser("get")
    live_get.add_argument("path")
    add_live_common(live_get)
    live_get.set_defaults(handler=command_live_get)

    live_list_controls = live_subparsers.add_parser("list-controls")
    add_live_common(live_list_controls)
    live_list_controls.set_defaults(handler=command_live_list_controls)

    live_list_sources = live_subparsers.add_parser("list-sources")
    add_live_common(live_list_sources)
    live_list_sources.set_defaults(handler=command_live_list_sources)

    live_connect = live_subparsers.add_parser("connect")
    live_connect.add_argument("source_module")
    live_connect.add_argument("target_path")
    live_connect.add_argument("--source-index", type=int, default=0)
    add_live_common(live_connect)
    live_connect.set_defaults(handler=command_live_connect)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    command = " ".join(part for part in [args.command, getattr(args, "list_command", None)] if part)
    try:
        response = args.handler(args)
        log_response(response)
        print(json.dumps(response, ensure_ascii=False))
        return 0
    except BespokeCliError as exc:
        response = error_response(exc, command)
        log_response(response)
        print(json.dumps(response, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
