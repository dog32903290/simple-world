const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 50 proof wires Lib.numbers.vec2.process nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-50-vec2-process-proof.vuo"), "utf8");
  for (const title of ["my_EaseVec2", "my_EaseVec2Keys", "my_SpringVec2", "my_Batch50Vec2ProcessProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-50-vec2-process-vuo-save/);
  assert.match(s, /ProofValue/);
});
