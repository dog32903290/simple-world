const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 48 proof wires Lib.numbers.float.random nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-48-float-random-proof.vuo"), "utf8");
  for (const title of ["my_FloatHash", "my_PerlinNoise", "my_Random", "my_Batch48FloatRandomProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-48-float-random-vuo-save/);
  assert.match(s, /ProofValue/);
});
