const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const poll = path.join(repoRoot, "tools/bespoke_live_poll.py");
const converter = path.join(repoRoot, "tools/bespoke_to_ingest.py");
const liveSample = path.join(repoRoot, "docs/runtime/fixtures/bespoke_live_controls_sample.json");

function mapRecords(inputPath) {
  const r = spawnSync("python3", [poll, "map-records", inputPath], { cwd: repoRoot, encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);
  return JSON.parse(r.stdout);
}

test("Live bridge records collapse into the flat module.control scalar map", () => {
  const map = mapRecords(liveSample);

  // continuous scalars survive, keyed module.name (bridging the `~` path gap)
  assert.equal(map["transport.tempo"], 147.0);
  assert.equal(map["transport.swing"], 0.5);
  assert.equal(map["gain.gain"], 0.4);

  // buttons, text entries, and stepped/enum controls are dropped
  assert.ok(!("transport. + " in map), "button dropped");
  assert.ok(!("transport.time signature" in map), "stepped/enum control dropped");
  assert.ok(!("savestate.filename" in map), "text entry dropped");

  assert.equal(Object.keys(map).length, 3, "only continuous scalars kept");
  for (const v of Object.values(map)) assert.equal(typeof v, "number");
});

test("Mapped controls feed cleanly through bespoke_to_ingest", () => {
  const map = mapRecords(liveSample);

  // wrap the flat map into the snapshot-log schema and run the real converter
  const snapLog = { fps: 60, snapshots: [{ t: 0.0, connected: true, controls: map }] };
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "blp-"));
  const snapPath = path.join(dir, "snap.json");
  const outPath = path.join(dir, "fixture.json");
  fs.writeFileSync(snapPath, JSON.stringify(snapLog));

  const r = spawnSync("python3", [converter, snapPath, outPath], { cwd: repoRoot, encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);

  const fixture = JSON.parse(fs.readFileSync(outPath, "utf8"));
  const params = fixture.messages.filter((m) => m.kind === "setValue").map((m) => m.param);
  assert.deepEqual(params.sort(), ["gain", "swing", "tempo"], "each scalar becomes a setValue param");
  assert.deepEqual(Object.values(fixture.trackMap).sort(), ["gain", "transport"], "modules become tracks");
});
