const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const tool = path.join(repoRoot, "tools/bespoke_to_ingest.py");
const sample = path.join(repoRoot, "docs/runtime/fixtures/bespoke_snapshots_sample.json");

function run(inputPath) {
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "b2i-")), "fixture.json");
  const r = spawnSync("python3", [tool, inputPath, out], { cwd: repoRoot, encoding: "utf8" });
  assert.equal(r.status, 0, r.stderr);
  return { fixture: JSON.parse(fs.readFileSync(out, "utf8")), outPath: out };
}

test("Bespoke control snapshots diff into a valid AUDIO_INGEST fixture", () => {
  const { fixture } = run(sample);

  // schema the engine + replay expect
  assert.equal(fixture.frameClock.fps, 60);
  assert.equal(fixture.ingest.policy, "hold");
  assert.ok(Array.isArray(fixture.messages));

  const kinds = fixture.messages.map((m) => m.kind);
  assert.equal(kinds[0], "connect", "first connected snapshot -> connect");
  assert.equal(kinds.at(-1), "disconnect", "connected:false -> disconnect");

  // module names map to stable int trackIds
  assert.deepEqual(
    Object.values(fixture.trackMap).sort(),
    ["filter", "reverb"],
    "each module becomes a track",
  );
  const sv = fixture.messages.filter((m) => m.kind === "setValue");
  for (const m of sv) {
    assert.ok(fixture.trackMap[String(m.track)] !== undefined, "setValue track is mapped");
    assert.equal(typeof m.param, "string");
    assert.equal(typeof m.value, "number");
  }
});

test("Only changed controls emit setValue (diff, not re-send)", () => {
  const { fixture } = run(sample);
  const sv = fixture.messages.filter((m) => m.kind === "setValue");
  // sample changes: t0 gain+cutoff (2), t0.25 cutoff (1), t0.5 gain (1); t0.75 nothing.
  assert.equal(sv.length, 4, "unchanged controls must not be re-emitted");
  // the unchanged cutoff at t0.75 produced no message
  assert.ok(!sv.some((m) => m.t === 0.75), "a frame with no change emits nothing");
});

test("Conversion is deterministic", () => {
  const a = run(sample).fixture;
  const b = run(sample).fixture;
  assert.deepEqual(a, b);
});
