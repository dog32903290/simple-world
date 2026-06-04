# Bespoke CLI Harness

Small agent-facing CLI for inspecting and safely mutating BespokeSynth `.bsk` files.

```bash
python3 tools/bespoke_cli/bespoke_cli.py status patch.bsk --json
python3 tools/bespoke_cli/bespoke_cli.py list modules patch.bsk --json
python3 tools/bespoke_cli/bespoke_cli.py list controls patch.bsk --module midicontroller2 --json
python3 tools/bespoke_cli/bespoke_cli.py set patch.bsk midicontroller2.devicein keyboard --out patched.bsk --json
python3 tools/bespoke_cli/bespoke_cli.py live status --wait --json
python3 tools/bespoke_cli/bespoke_cli.py live list-controls --wait --json
python3 tools/bespoke_cli/bespoke_cli.py live list-sources --wait --json
python3 tools/bespoke_cli/bespoke_cli.py live get looper2~volume --wait --json
python3 tools/bespoke_cli/bespoke_cli.py live set looper2~volume 0.7 --wait --json
python3 tools/bespoke_cli/bespoke_cli.py live connect audioleveltocv looper2~volume --source-index 0 --wait --json
```

The CLI preserves the binary tail after the JSON section and writes JSONL logs to `logs/bespoke-cli/run.jsonl`.

## MCP Server

The Codex MCP wrapper lives at:

```text
tools/bespoke_mcp/server.py
```

It exposes the CLI as MCP tools:

```text
bespoke_patch_status
bespoke_patch_validate
bespoke_patch_list_modules
bespoke_patch_list_controls
bespoke_patch_set_control
bespoke_live_status
bespoke_live_list_controls
bespoke_live_list_sources
bespoke_live_get
bespoke_live_set
bespoke_live_connect
```

The server is stdio MCP and is registered in `~/.codex/config.toml` as `bespoke`.

## Live Bridge

The installed `/Applications/BespokeSynth.app` includes a native C++ bridge listening on OSC port `9001`.

Live CLI commands send base64 JSON to `/bespoke-agent/json`; Bespoke writes the latest result to:

```text
/tmp/bespoke_agent_bridge_status.json
```

Use `--wait` on live commands when an agent needs proof that Bespoke handled the command, not only proof that UDP sent a packet.

`live connect` is deliberately narrow: it connects an existing source module cable outlet (`--source-index`) to an existing module or UI control target path. Bespoke itself validates the target family through `PatchCableSource::FindValidTargets()`.

Use `live list-sources` before `live connect` to discover each module outlet index, connection family, enabled state, and current targets from the running Bespoke instance.

Focused test:

```bash
python3 -m unittest tests/test_bespoke_cli.py tests/test_bespoke_mcp.py
```
