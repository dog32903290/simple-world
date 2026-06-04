import base64
import json
import time


PORT = 9001
DEFAULT_STATUS_PATH = "/tmp/bespoke_agent_bridge_status.json"

me.connect_osc_input(PORT)


def _write_status(path, payload):
    payload["bridge"] = "bespoke-agent-bridge/v0"
    payload["port"] = PORT
    payload["time"] = time.time()
    with open(path or DEFAULT_STATUS_PATH, "w", encoding="utf-8") as f:
        json.dump(payload, f)


def _decode_payload(message):
    parts = message.split(" ", 1)
    if len(parts) != 2 or parts[0] != "/bespoke-agent/json":
        return None
    return json.loads(base64.b64decode(parts[1]).decode("utf-8"))


def on_osc(message):
    status_path = DEFAULT_STATUS_PATH
    try:
        payload = _decode_payload(message)
        if payload is None:
            _write_status(status_path, {"ok": False, "error": "unsupported_message", "message": message})
            return

        status_path = payload.get("status_path", DEFAULT_STATUS_PATH)
        op = payload.get("op")

        if op == "status":
            _write_status(status_path, {"ok": True, "op": "status", "message": "bridge alive"})
            return

        if op == "set":
            path = payload["path"]
            value = payload["value"]
            me.set(path, value)
            _write_status(status_path, {"ok": True, "op": "set", "path": path, "value": value})
            return

        _write_status(status_path, {"ok": False, "error": "unknown_op", "op": op})
    except Exception as exc:
        _write_status(status_path, {"ok": False, "error": "exception", "message": str(exc)})
