#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


PROTOCOL_VERSION = "2025-06-18"
ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "tools" / "bespoke_cli" / "bespoke_cli.py"


TOOLS = [
    {
        "name": "bespoke_patch_status",
        "description": "Inspect a BespokeSynth .bsk or extensionless save file.",
        "inputSchema": {
            "type": "object",
            "properties": {"patch": {"type": "string"}},
            "required": ["patch"],
        },
    },
    {
        "name": "bespoke_patch_validate",
        "description": "Validate the BespokeSynth patch header, JSON section, module list, and binary tail.",
        "inputSchema": {
            "type": "object",
            "properties": {"patch": {"type": "string"}},
            "required": ["patch"],
        },
    },
    {
        "name": "bespoke_patch_list_modules",
        "description": "List modules in a BespokeSynth patch.",
        "inputSchema": {
            "type": "object",
            "properties": {"patch": {"type": "string"}},
            "required": ["patch"],
        },
    },
    {
        "name": "bespoke_patch_list_controls",
        "description": "List scalar JSON controls and saved connections for one module in a BespokeSynth patch.",
        "inputSchema": {
            "type": "object",
            "properties": {"patch": {"type": "string"}, "module": {"type": "string"}},
            "required": ["patch", "module"],
        },
    },
    {
        "name": "bespoke_patch_set_control",
        "description": "Write one scalar JSON control in a patch to a new output file while preserving the binary tail.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "patch": {"type": "string"},
                "path": {"type": "string", "description": "module.field path returned by bespoke_patch_list_controls."},
                "value": {"type": ["string", "number", "boolean", "null"]},
                "out": {"type": "string"},
            },
            "required": ["patch", "path", "value", "out"],
        },
    },
    {
        "name": "bespoke_live_status",
        "description": "Send a status probe to the installed BespokeSynth native agent bridge on OSC port 9001.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
            },
        },
    },
    {
        "name": "bespoke_live_set",
        "description": "Set one numeric live Bespoke UI control through the installed native agent bridge.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Bespoke UI path such as looper2~volume."},
                "value": {"type": ["string", "number", "boolean"]},
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
            },
            "required": ["path", "value"],
        },
    },
    {
        "name": "bespoke_live_get",
        "description": "Get one live Bespoke UI control value through the installed native agent bridge.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "path": {"type": "string", "description": "Bespoke UI path such as looper2~volume."},
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
                "timeout": {"type": "number", "default": 2.0},
            },
            "required": ["path"],
        },
    },
    {
        "name": "bespoke_live_list_controls",
        "description": "List live Bespoke UI controls through the installed native agent bridge.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
                "timeout": {"type": "number", "default": 2.0},
            },
        },
    },
    {
        "name": "bespoke_live_list_sources",
        "description": "List live Bespoke patch cable source outlets through the installed native agent bridge.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
                "timeout": {"type": "number", "default": 2.0},
            },
        },
    },
    {
        "name": "bespoke_live_connect",
        "description": "Connect one live Bespoke patch cable source to a module or UI control target path.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "source_module": {"type": "string", "description": "Source module path/name."},
                "target_path": {"type": "string", "description": "Target module path or UI control path."},
                "source_index": {"type": "integer", "default": 0},
                "host": {"type": "string", "default": "127.0.0.1"},
                "port": {"type": "integer", "default": 9001},
                "status_file": {"type": "string", "default": "/tmp/bespoke_agent_bridge_status.json"},
                "timeout": {"type": "number", "default": 2.0},
            },
            "required": ["source_module", "target_path"],
        },
    },
]


def log(message):
    print(f"[bespoke-mcp] {message}", file=sys.stderr, flush=True)


def read_message():
    first = sys.stdin.buffer.readline()
    if not first:
        return None

    if first.startswith(b"Content-Length:"):
        length = int(first.split(b":", 1)[1].strip())
        while True:
            line = sys.stdin.buffer.readline()
            if line in (b"\n", b"\r\n", b""):
                break
        return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))

    return json.loads(first.decode("utf-8"))


def write_message(message):
    payload = json.dumps(message, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(b"Content-Length: " + str(len(payload)).encode("ascii") + b"\r\n\r\n" + payload)
    sys.stdout.buffer.flush()


def response(request_id, result=None, error=None):
    message = {"jsonrpc": "2.0", "id": request_id}
    if error is None:
        message["result"] = result if result is not None else {}
    else:
        message["error"] = error
    write_message(message)


def cli_value(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    return str(value)


def run_cli(args):
    proc = subprocess.run(
        [sys.executable, str(CLI), *args, "--json"],
        cwd=str(ROOT),
        text=True,
        capture_output=True,
        check=False,
    )
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError:
        payload = {
            "ok": False,
            "protocol": "bespoke-mcp/v0",
            "error": {
                "type": "cli_output_not_json",
                "message": "bespoke_cli.py did not return JSON on stdout.",
                "repair_hint": "Run the command directly and inspect stderr.",
                "retryable": False,
            },
            "context": {"stdout": proc.stdout, "stderr": proc.stderr, "returncode": proc.returncode},
        }
    if proc.stderr:
        payload.setdefault("context", {})["stderr"] = proc.stderr
    payload.setdefault("backend", {})["cli"] = str(CLI)
    return payload, proc.returncode


def tool_result(payload, returncode):
    return {
        "content": [{"type": "text", "text": json.dumps(payload, ensure_ascii=False)}],
        "isError": returncode != 0 or payload.get("ok") is False,
    }


def call_tool(name, arguments):
    arguments = arguments or {}
    if name == "bespoke_patch_status":
        return run_cli(["status", arguments["patch"]])
    if name == "bespoke_patch_validate":
        return run_cli(["validate", arguments["patch"]])
    if name == "bespoke_patch_list_modules":
        return run_cli(["list", "modules", arguments["patch"]])
    if name == "bespoke_patch_list_controls":
        return run_cli(["list", "controls", arguments["patch"], "--module", arguments["module"]])
    if name == "bespoke_patch_set_control":
        return run_cli(["set", arguments["patch"], arguments["path"], cli_value(arguments["value"]), "--out", arguments["out"]])
    if name == "bespoke_live_status":
        args = ["live", "status", "--wait"]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)
    if name == "bespoke_live_set":
        args = ["live", "set", arguments["path"], cli_value(arguments["value"]), "--wait"]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)
    if name == "bespoke_live_get":
        args = ["live", "get", arguments["path"], "--wait"]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)
    if name == "bespoke_live_list_controls":
        args = ["live", "list-controls", "--wait"]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)
    if name == "bespoke_live_list_sources":
        args = ["live", "list-sources", "--wait"]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)
    if name == "bespoke_live_connect":
        args = [
            "live",
            "connect",
            arguments["source_module"],
            arguments["target_path"],
            "--source-index",
            cli_value(arguments.get("source_index", 0)),
            "--wait",
        ]
        if "host" in arguments:
            args.extend(["--host", arguments["host"]])
        if "port" in arguments:
            args.extend(["--port", cli_value(arguments["port"])])
        if "status_file" in arguments:
            args.extend(["--status-file", arguments["status_file"]])
        if "timeout" in arguments:
            args.extend(["--timeout", cli_value(arguments["timeout"])])
        return run_cli(args)

    return {
        "ok": False,
        "protocol": "bespoke-mcp/v0",
        "error": {
            "type": "unknown_tool",
            "message": f"Unknown Bespoke MCP tool: {name}",
            "repair_hint": "Call tools/list and use one of the advertised tool names.",
            "retryable": False,
        },
    }, 1


def handle(message):
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}

    if method == "initialize":
        response(
            request_id,
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "bespoke-mcp", "version": "0.1.0"},
            },
        )
        return
    if method == "tools/list":
        response(request_id, {"tools": TOOLS})
        return
    if method == "tools/call":
        payload, returncode = call_tool(params.get("name"), params.get("arguments") or {})
        response(request_id, tool_result(payload, returncode))
        return
    if method == "ping":
        response(request_id, {})
        return
    if request_id is not None and method not in ("notifications/initialized",):
        response(
            request_id,
            error={
                "code": -32601,
                "message": f"Unsupported method: {method}",
            },
        )


def main():
    log(f"serving with CLI {CLI}")
    while True:
        message = read_message()
        if message is None:
            return 0
        try:
            handle(message)
        except Exception as exc:
            log(f"error: {exc}")
            request_id = message.get("id") if isinstance(message, dict) else None
            if request_id is not None:
                response(request_id, error={"code": -32603, "message": str(exc)})


if __name__ == "__main__":
    raise SystemExit(main())
