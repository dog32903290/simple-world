const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/AUDIO_INGEST_CONTRACT.md");
const runtimeContractPath = path.join(repoRoot, "docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md");
const fixturePath = path.join(repoRoot, "docs/runtime/fixtures/audio_ingest_semantic_log.json");

// Behavioral authority is the C++ `--selftest-audioingest` (latest-wins + smoothing,
// state-based self-heal, idempotent pulse, source-absent cookable). This JS suite is
// the contract/fixture gate: the law is present, well-formed, and the recorded log
// actually exercises each property the selftest asserts.

test("AudioIngest contract draws the external-audio boundary and owns the clock crossing", () => {
  const c = fs.readFileSync(contractPath, "utf8");
  assert.match(c, /sound comes from outside/i);
  assert.match(c, /realtime audio engine/i);
  assert.match(c, /owns \*\*no\*\* audio callback/i);
  assert.match(c, /One Canonical Representation/);
  assert.match(c, /Clock Crossing Is Owned Here/);
  assert.match(c, /no shared sample clock/i);
  assert.match(c, /Source-Absent Is a Defined State/);
  // honest non-goal + named coupling
  assert.match(c, /Sample-accurate audio.+visual sync.+out of scope|no shared clock/i);
  assert.match(c, /incomplete standalone, by design/i);
});

test("Runtime contract retires the internal audio domain in favor of external ingest", () => {
  const r = fs.readFileSync(runtimeContractPath, "utf8");
  assert.match(r, /## External Audio Boundary/);
  assert.match(r, /not an internal cook domain/);
  assert.match(r, /AUDIO_INGEST_CONTRACT\.md/);
  // AudioFrame type row reinterpreted as external source
  assert.match(r, /AudioFrame.+external/);
});

test("Fixture is well-formed and exercises every asserted property", () => {
  const g = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
  assert.equal(g.graphId, "fixture.audio_ingest_semantic_log");
  assert.equal(g.frameClock.fps, 60);
  assert.ok(Array.isArray(g.messages));

  const byKind = (k) => g.messages.filter((m) => m.kind === k);

  // latest-wins: two setValue for the same param inside one frame window (<= 1/60s)
  const cutoffs = byKind("setValue").filter((m) => m.param === "cutoff");
  assert.ok(cutoffs.length >= 2, "needs >=2 cutoff writes to prove latest-wins");
  const sameFrame = cutoffs.filter((m) => m.t > 0 && m.t < 1 / 60);
  assert.ok(sameFrame.length >= 2, "two cutoff writes must land in the same frame");

  // idempotent pulse: a duplicate seq
  const pulses = byKind("pulse");
  assert.ok(pulses.length >= 2 && pulses[0].value === pulses[1].value,
    "needs a duplicate pulse seq to prove idempotency");

  // state-based self-heal: a notesState that releases (empty notes) with no edge noteOff
  const states = byKind("notesState");
  assert.ok(states.some((m) => Array.isArray(m.notes) && m.notes.length > 0), "a note must turn on");
  assert.ok(states.some((m) => Array.isArray(m.notes) && m.notes.length === 0),
    "an authoritative empty notesState must self-heal the dropped release");

  // source-absent is exercised
  assert.ok(byKind("disconnect").length >= 1, "must disconnect to exercise source-absent");

  // documented expectations match the contract numbers
  assert.equal(g.expect.pulseCount, 1);
  assert.equal(g.expect.frame1.values["0/cutoff"], 0.32);
  assert.equal(g.expect.frame7.connected, false);
  assert.equal(g.expect.frame7.cookable, true);
  assert.equal(g.expect.frame7.noteHeld_0_60, false);
});
