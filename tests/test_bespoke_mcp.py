import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "tools" / "bespoke_mcp" / "server.py"


def frame(message):
    payload = json.dumps(message).encode("utf-8")
    return b"Content-Length: " + str(len(payload)).encode("ascii") + b"\r\n\r\n" + payload


def read_frame(stream):
    header = stream.readline()
    if not header:
        raise AssertionError("MCP server closed stdout")
    assert header.startswith(b"Content-Length:"), header
    length = int(header.split(b":", 1)[1].strip())
    blank = stream.readline()
    assert blank in (b"\n", b"\r\n"), blank
    return json.loads(stream.read(length).decode("utf-8"))


def write_bsk(path, patch_json, tail=b"MODULE_STATE"):
    payload = json.dumps(patch_json, separators=(",", ":")).encode("utf-8")
    path.write_bytes(len(payload).to_bytes(4, "little") + (0).to_bytes(4, "little") + payload + tail)


class BespokeMcpTests(unittest.TestCase):
    def test_lists_tools_and_calls_patch_status(self):
        tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(tmpdir.cleanup)
        patch = Path(tmpdir.name) / "minimal.bsk"
        write_bsk(
            patch,
            {
                "modules": [
                    {
                        "type": "midicontroller",
                        "name": "midicontroller2",
                        "position": [10.0, 20.0],
                    }
                ],
                "zoomlocations": [],
            },
        )

        proc = subprocess.Popen(
            [sys.executable, str(SERVER)],
            cwd=str(ROOT),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            proc.stdin.write(frame({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}))
            proc.stdin.flush()
            initialized = read_frame(proc.stdout)
            self.assertEqual(initialized["result"]["serverInfo"]["name"], "bespoke-mcp")

            proc.stdin.write(frame({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}))
            proc.stdin.flush()
            tools = read_frame(proc.stdout)["result"]["tools"]
            self.assertIn("bespoke_patch_status", {tool["name"] for tool in tools})
            self.assertIn("bespoke_live_get", {tool["name"] for tool in tools})
            self.assertIn("bespoke_live_list_controls", {tool["name"] for tool in tools})
            self.assertIn("bespoke_live_list_sources", {tool["name"] for tool in tools})
            self.assertIn("bespoke_live_connect", {tool["name"] for tool in tools})

            proc.stdin.write(
                frame(
                    {
                        "jsonrpc": "2.0",
                        "id": 3,
                        "method": "tools/call",
                        "params": {"name": "bespoke_patch_status", "arguments": {"patch": str(patch)}},
                    }
                )
            )
            proc.stdin.flush()
            result = read_frame(proc.stdout)["result"]
            payload = json.loads(result["content"][0]["text"])
            self.assertTrue(payload["ok"])
            self.assertEqual(payload["state"]["module_count"], 1)
        finally:
            proc.kill()
            proc.wait(timeout=5)
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()


if __name__ == "__main__":
    unittest.main()
