import json
import base64
import socket
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "bespoke_cli" / "bespoke_cli.py"


def write_bsk(path, patch_json, tail=b"MODULE_STATE"):
    payload = json.dumps(patch_json, separators=(",", ":")).encode("utf-8")
    path.write_bytes(len(payload).to_bytes(4, "little") + (0).to_bytes(4, "little") + payload + tail)


def run_cli(*args):
    return subprocess.run(
        [sys.executable, str(CLI), *args, "--json"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


class BespokeCliTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)
        self.patch_path = Path(self.tmpdir.name) / "fixture.bsk"
        self.patch_json = {
            "modules": [
                {
                    "type": "midicontroller",
                    "name": "midicontroller2",
                    "position": [10.0, 20.0],
                    "devicein": "keyboard",
                    "connections": [
                        {
                            "type": "note",
                            "control": 49,
                            "page": 0,
                            "uicontrol": "pulser~enabled",
                            "toggle": True,
                        }
                    ],
                },
                {
                    "type": "pulser",
                    "name": "pulser",
                    "position": [200.0, 100.0],
                    "target": "",
                    "enabled": False,
                },
            ],
            "zoomlocations": [],
        }
        write_bsk(self.patch_path, self.patch_json)

    def test_status_reports_patch_shape(self):
        result = run_cli("status", str(self.patch_path))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data["ok"])
        self.assertEqual(data["command"], "status")
        self.assertEqual(data["state"]["module_count"], 2)
        self.assertGreater(data["state"]["json_bytes"], 0)
        self.assertGreater(data["state"]["binary_tail_bytes"], 0)

    def test_list_modules_reports_module_contracts(self):
        result = run_cli("list", "modules", str(self.patch_path))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        modules = data["modules"]
        self.assertEqual([m["name"] for m in modules], ["midicontroller2", "pulser"])
        self.assertEqual(modules[0]["type"], "midicontroller")
        self.assertEqual(modules[0]["connection_count"], 1)

    def test_list_controls_reports_module_fields_and_connections(self):
        result = run_cli("list", "controls", str(self.patch_path), "--module", "midicontroller2")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertIn({"path": "midicontroller2.devicein", "value": "keyboard"}, data["controls"])
        self.assertEqual(data["connections"][0]["uicontrol"], "pulser~enabled")

    def test_set_writes_new_bsk_and_preserves_binary_tail(self):
        out_path = Path(self.tmpdir.name) / "patched.bsk"
        result = run_cli("set", str(self.patch_path), "midicontroller2.devicein", "HandMIDI Virtual", "--out", str(out_path))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data["ok"])
        self.assertEqual(data["mutation"]["old_value"], "keyboard")
        self.assertEqual(data["mutation"]["new_value"], "HandMIDI Virtual")

        raw = out_path.read_bytes()
        json_len = int.from_bytes(raw[:4], "little")
        patched = json.loads(raw[8 : 8 + json_len])
        self.assertEqual(patched["modules"][0]["devicein"], "HandMIDI Virtual")
        self.assertEqual(raw[8 + json_len :], b"MODULE_STATE")

    def test_missing_module_fails_with_typed_error(self):
        result = run_cli("list", "controls", str(self.patch_path), "--module", "missing")
        self.assertNotEqual(result.returncode, 0)
        data = json.loads(result.stdout)
        self.assertFalse(data["ok"])
        self.assertEqual(data["error"]["type"], "module_not_found")

    def test_live_status_sends_base64_json_osc_packet(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            result = run_cli(
                "live",
                "status",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(Path(self.tmpdir.name) / "bridge-status.json"),
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            packet, _ = server.recvfrom(4096)
            osc_strings = decode_osc_strings(packet)
            self.assertEqual(osc_strings[0], "/bespoke-agent/json")
            self.assertEqual(osc_strings[1], ",s")
            payload = json.loads(base64.b64decode(osc_strings[2]).decode("utf-8"))
            self.assertEqual(payload["op"], "status")
            self.assertEqual(payload["status_path"], str(Path(self.tmpdir.name) / "bridge-status.json"))

    def test_live_set_sends_path_and_typed_value(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            result = run_cli(
                "live",
                "set",
                "looper2~volume",
                "0.75",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            packet, _ = server.recvfrom(4096)
            payload = json.loads(base64.b64decode(decode_osc_strings(packet)[2]).decode("utf-8"))
            self.assertEqual(payload["op"], "set")
            self.assertEqual(payload["path"], "looper2~volume")
            self.assertEqual(payload["value"], 0.75)

    def test_live_status_waits_for_bridge_ack(self):
        status_path = Path(self.tmpdir.name) / "bridge-status.json"

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            def bridge_ack():
                server.recvfrom(4096)
                status_path.write_text(json.dumps({"ok": True, "op": "status", "bridge": "test-bridge"}))

            thread = threading.Thread(target=bridge_ack)
            thread.start()
            result = run_cli(
                "live",
                "status",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(status_path),
                "--wait",
            )
            thread.join(timeout=2)

            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data["bridge_result"]["bridge"], "test-bridge")

    def test_live_get_sends_path_and_waits_for_value(self):
        status_path = Path(self.tmpdir.name) / "bridge-get.json"

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            def bridge_ack():
                packet, _ = server.recvfrom(4096)
                payload = json.loads(base64.b64decode(decode_osc_strings(packet)[2]).decode("utf-8"))
                self.assertEqual(payload["op"], "get")
                self.assertEqual(payload["path"], "looper2~volume")
                status_path.write_text(json.dumps({"ok": True, "op": "get", "path": payload["path"], "value": 0.42}))

            thread = threading.Thread(target=bridge_ack)
            thread.start()
            result = run_cli(
                "live",
                "get",
                "looper2~volume",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(status_path),
                "--wait",
            )
            thread.join(timeout=2)

            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data["bridge_result"]["value"], 0.42)

    def test_live_list_controls_waits_for_control_inventory(self):
        status_path = Path(self.tmpdir.name) / "bridge-list-controls.json"

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            def bridge_ack():
                packet, _ = server.recvfrom(4096)
                payload = json.loads(base64.b64decode(decode_osc_strings(packet)[2]).decode("utf-8"))
                self.assertEqual(payload["op"], "list_controls")
                status_path.write_text(
                    json.dumps(
                        {
                            "ok": True,
                            "op": "list_controls",
                            "controls": [
                                {"path": "looper2~volume", "module": "looper2", "name": "volume", "value": 0.5}
                            ],
                        }
                    )
                )

            thread = threading.Thread(target=bridge_ack)
            thread.start()
            result = run_cli(
                "live",
                "list-controls",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(status_path),
                "--wait",
            )
            thread.join(timeout=2)

            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data["bridge_result"]["controls"][0]["path"], "looper2~volume")

    def test_live_list_sources_waits_for_source_inventory(self):
        status_path = Path(self.tmpdir.name) / "bridge-list-sources.json"

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            def bridge_ack():
                packet, _ = server.recvfrom(4096)
                payload = json.loads(base64.b64decode(decode_osc_strings(packet)[2]).decode("utf-8"))
                self.assertEqual(payload["op"], "list_sources")
                status_path.write_text(
                    json.dumps(
                        {
                            "ok": True,
                            "op": "list_sources",
                            "sources": [
                                {
                                    "module": "pulser",
                                    "module_type": "pulser",
                                    "index": 0,
                                    "connection_type": "pulse",
                                    "target_path": "looper2~mute",
                                }
                            ],
                        }
                    )
                )

            thread = threading.Thread(target=bridge_ack)
            thread.start()
            result = run_cli(
                "live",
                "list-sources",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(status_path),
                "--wait",
            )
            thread.join(timeout=2)

            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data["bridge_result"]["sources"][0]["connection_type"], "pulse")

    def test_live_connect_sends_source_and_target(self):
        status_path = Path(self.tmpdir.name) / "bridge-connect.json"

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(("127.0.0.1", 0))
            server.settimeout(2)
            port = server.getsockname()[1]

            def bridge_ack():
                packet, _ = server.recvfrom(4096)
                payload = json.loads(base64.b64decode(decode_osc_strings(packet)[2]).decode("utf-8"))
                self.assertEqual(payload["op"], "connect")
                self.assertEqual(payload["source_module"], "audioleveltocv")
                self.assertEqual(payload["source_index"], 0)
                self.assertEqual(payload["target_path"], "looper2~volume")
                status_path.write_text(
                    json.dumps(
                        {
                            "ok": True,
                            "op": "connect",
                            "source_module": payload["source_module"],
                            "source_index": payload["source_index"],
                            "target_path": payload["target_path"],
                            "connection_type": "modulator",
                        }
                    )
                )

            thread = threading.Thread(target=bridge_ack)
            thread.start()
            result = run_cli(
                "live",
                "connect",
                "audioleveltocv",
                "looper2~volume",
                "--source-index",
                "0",
                "--host",
                "127.0.0.1",
                "--port",
                str(port),
                "--status-file",
                str(status_path),
                "--wait",
            )
            thread.join(timeout=2)

            self.assertEqual(result.returncode, 0, result.stderr)
            data = json.loads(result.stdout)
            self.assertEqual(data["bridge_result"]["connection_type"], "modulator")


def decode_osc_strings(packet):
    strings = []
    offset = 0
    while offset < len(packet):
        end = packet.index(b"\0", offset)
        strings.append(packet[offset:end].decode("utf-8"))
        offset = (end + 4) & ~3
    return strings


if __name__ == "__main__":
    unittest.main()
