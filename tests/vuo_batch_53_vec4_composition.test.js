const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 53 proof wires Lib.numbers.vec4 nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-53-vec4-proof.vuo"), "utf8");
  for (const title of ["my_DotVec4", "my_PickColor", "my_RgbaToColor", "my_Vector4Components", "my_Batch53Vec4Proof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-53-vec4-vuo-save/);
  assert.match(s, /ProofValue/);
});
