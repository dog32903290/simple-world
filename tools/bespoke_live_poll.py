#!/usr/bin/env python3
"""Poll a live BespokeSynth over the OSC bridge into a snapshot log.

This is the missing layer between `tools/bespoke_cli` (live bridge) and
`tools/bespoke_to_ingest.py` (pure transform). The bridge's `list-controls`
returns *structured* records whose path uses `~`:

    { "module": "transport", "name": "tempo", "value": 147.0,
      "path": "transport~tempo", "is_button": false, "num_values": 0 }

`bespoke_to_ingest.py` wants the *flat* snapshot schema it documents:

    { "fps": 60, "snapshots": [ { "t", "connected", "controls": {"module.control": value} } ] }

`scalar_map()` is that bridge: it collapses live records into the flat map,
keeping only continuous scalars (drops buttons, text entries, stepped/enum
controls). It is pure and host-independent so it is unit-tested without a
running Bespoke (see tests/bespoke_live_poll.test.js). The live `record()`
loop needs a running BespokeSynth + OSC bridge.

    # record (read-only) ~3s of live values, then convert + replay:
    python3 tools/bespoke_live_poll.py record snapshots.json --frames 6 --interval 0.3
    python3 tools/bespoke_to_ingest.py snapshots.json out.json
    ./app/build/simple_world --audio-ingest-replay out.json

    # optionally inject motion on one control to prove changing values flow:
    python3 tools/bespoke_live_poll.py record snapshots.json \
        --sweep transport~tempo 147,160,175,190,175,160 --restore

    # host-independent mapping check (no Bespoke; used by tests):
    python3 tools/bespoke_live_poll.py map-records live_controls.json
"""
import json
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CLI = ["python3", str(REPO_ROOT / "tools/bespoke_cli/bespoke_cli.py")]


def scalar_map(controls):
    """Live-bridge control records -> {"module.name": float} for continuous scalars.

    Drops buttons, text entries, and stepped/enum controls (num_values not in
    {0, None}); these are not continuous audio-rate values. Keys join module and
    name with '.', matching the snapshot schema bespoke_to_ingest.py splits on.
    """
    out = {}
    for c in controls:
        if c.get("is_button") or c.get("is_text_entry"):
            continue
        if c.get("num_values", 0) not in (0, None):
            continue
        try:
            out[f"{c['module']}.{c['name']}"] = float(c["value"])
        except (KeyError, TypeError, ValueError):
            continue
    return out


# ---- live bridge calls (require a running BespokeSynth) ----------------------

def _run_cli(args, attempts=2):
    # The OSC bridge occasionally drops a --wait response under rapid calls;
    # one retry keeps a sweep from failing mid-flight and leaving the live
    # patch dirty (the finally-restore only fires once orig was read).
    last = None
    for _ in range(attempts):
        p = subprocess.run(CLI + args, capture_output=True, text=True)
        if p.returncode == 0:
            return json.loads(p.stdout)
        last = p
        time.sleep(0.2)
    sys.stderr.write(last.stderr)
    raise SystemExit(
        f"bespoke_cli failed (rc={last.returncode}): {' '.join(args)}\nstdout: {last.stdout[:600]}"
    )


def _list_controls(timeout):
    d = _run_cli(["live", "list-controls", "--wait", "--json", "--timeout", str(timeout)])
    return d.get("bridge_result", {}).get("controls", [])


def _live_get(path, timeout):
    d = _run_cli(["live", "get", path, "--wait", "--json", "--timeout", str(timeout)])
    return d.get("bridge_result", {})


def _live_set(path, value, timeout):
    _run_cli(["live", "set", path, str(value), "--wait", "--json", "--timeout", str(timeout)])


def record(out_path, frames, interval, fps, timeout, sweep_path=None, sweep_values=None, restore=False):
    orig = None
    if sweep_path and restore:
        orig = float(_live_get(sweep_path, timeout).get("value", 0.0))
        sys.stderr.write(f"[poll] original {sweep_path} = {orig}\n")

    snapshots = []
    t0 = time.monotonic()
    try:
        for i in range(frames):
            if sweep_path and sweep_values:
                _live_set(sweep_path, sweep_values[i % len(sweep_values)], timeout)
                time.sleep(0.05)  # let the bridge apply before reading back
            controls = scalar_map(_list_controls(timeout))
            t = round(time.monotonic() - t0, 4)
            snapshots.append({"t": t, "connected": True, "controls": controls})
            sys.stderr.write(f"[poll] frame {i} t={t} ({len(controls)} scalars)\n")
            if interval and i < frames - 1:
                time.sleep(interval)
    finally:
        if orig is not None:
            _live_set(sweep_path, orig, timeout)
            sys.stderr.write(f"[poll] restored {sweep_path} = {orig}\n")

    Path(out_path).write_text(json.dumps({"fps": fps, "snapshots": snapshots}, indent=2) + "\n")
    print(f"[poll] wrote {len(snapshots)} snapshots -> {out_path}")
    return 0


# ---- host-independent mapping (no Bespoke) -----------------------------------

def map_records(records_path):
    """Read live-bridge control records from a file, print the flat scalar map."""
    data = json.loads(Path(records_path).read_text())
    controls = data.get("controls", data) if isinstance(data, dict) else data
    print(json.dumps(scalar_map(controls), indent=2))
    return 0


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "map-records":
        if len(argv) < 3:
            sys.stderr.write("usage: bespoke_live_poll.py map-records <live_controls.json>\n")
            return 2
        return map_records(argv[2])
    if cmd == "record":
        if len(argv) < 3:
            sys.stderr.write("usage: bespoke_live_poll.py record <out.json> [opts]\n")
            return 2
        out_path = argv[2]
        rest = argv[3:]

        def opt(name, default):
            return rest[rest.index(name) + 1] if name in rest else default

        frames = int(opt("--frames", "6"))
        interval = float(opt("--interval", "0.3"))
        fps = int(opt("--fps", "60"))
        timeout = float(opt("--timeout", "8"))
        sweep_path, sweep_values = None, None
        if "--sweep" in rest:
            j = rest.index("--sweep")
            sweep_path = rest[j + 1]
            sweep_values = [float(x) for x in rest[j + 2].split(",")]
        restore = "--restore" in rest
        return record(out_path, frames, interval, fps, timeout, sweep_path, sweep_values, restore)

    sys.stderr.write(f"unknown command: {cmd}\n")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
