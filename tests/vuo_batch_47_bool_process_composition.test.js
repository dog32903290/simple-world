const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 47 proof wires Lib.numbers.bool.process nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-47-bool-process-proof.vuo"), "utf8");
  for (const title of ["my_CacheBoolean", "my_DelayBoolean", "my_DelayTriggerChange", "my_KeepBoolean", "my_Batch47BoolProcessProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-47-bool-process-vuo-save/);
  assert.match(s, /ProofValue/);
});
