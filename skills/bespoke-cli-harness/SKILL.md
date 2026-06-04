---
name: bespoke-cli-harness
description: Use when inspecting, validating, or mutating BespokeSynth .bsk files through the local bespoke-cli harness.
---

# Bespoke CLI Harness

Use this skill when an agent needs to inspect or safely edit BespokeSynth `.bsk` patch files without using the GUI.

## Entrypoint

```bash
python3 tools/bespoke_cli/bespoke_cli.py <command> ... --json
```

## Backend

This v0 harness uses the real Bespoke `.bsk` file format:

```text
4-byte little-endian JSON length
4-byte reserved header
UTF-8 patch JSON
binary module save-state tail
```

The CLI does not launch BespokeSynth or drive the GUI. It also speaks to the installed BespokeSynth native OSC bridge for live status/get/set commands. The MCP server wraps this CLI rather than duplicating Bespoke file or OSC behavior.

## MCP Entrypoint

```bash
python3 tools/bespoke_mcp/server.py
```

Registered Codex MCP server name:

```text
bespoke
```

MCP tools:

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

## Commands

```bash
python3 tools/bespoke_cli/bespoke_cli.py status patch.bsk --json
python3 tools/bespoke_cli/bespoke_cli.py validate patch.bsk --json
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

## Contract

- Always request `--json`.
- Mutations write to `--out`; they do not overwrite the input path.
- Mutations preserve the binary module save-state tail.
- Failures return typed JSON errors on stdout and a nonzero exit code.
- Logs are appended to `logs/bespoke-cli/run.jsonl`.

## Live Bridge

The installed `/Applications/BespokeSynth.app` includes a native C++ agent bridge. No `script` module is required.

The bridge listens on UDP OSC port `9001`.

Live commands send OSC address `/bespoke-agent/json` with one base64 JSON string argument. The bridge writes execution results to:

```text
/tmp/bespoke_agent_bridge_status.json
```

Check that file after live commands. UDP send success alone only proves the packet left the CLI.
Use `--wait` when calling live commands through the CLI. The MCP wrapper does this by default and returns a typed `bridge_ack_timeout` failure if Bespoke does not ACK.
`live connect` only connects an existing source cable outlet to an existing module/control target path. It does not create modules, infer cable families, or choose source outlets automatically.
Run `live list-sources` before `live connect` when the outlet index or connection family is unknown.

## Verification

Run the focused test:

```bash
python3 -m unittest tests/test_bespoke_cli.py tests/test_bespoke_mcp.py
```

For a real patch smoke check:

```bash
python3 tools/bespoke_cli/bespoke_cli.py status /Users/chenbaiwei/Documents/BespokeSynth/tmp --json
```

Note: Bespoke command-line loading requires `.bsk` or `.bskt` extension, but this CLI can inspect extensionless save files if their bytes are valid.

## Limitations

- v0 only handles top-level module scalar fields.
- v0 does not inspect native module binary state after the JSON section.
- v0 does not create modules.
- Live control currently supports `status`, `list-controls`, `list-sources`, `get`, `set`, and narrow `connect` through the native Bespoke C++ OSC bridge.
