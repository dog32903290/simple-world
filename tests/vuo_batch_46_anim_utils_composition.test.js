const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 46 proof wires Lib.numbers.anim.utils nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-46-anim-utils-proof.vuo"), "utf8");
  for (const title of ["my_FindKeyframes", "my_SetKeyframes", "my_Batch46AnimUtilsProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-46-anim-utils-vuo-save/);
  assert.match(s, /ProofValue/);
});
