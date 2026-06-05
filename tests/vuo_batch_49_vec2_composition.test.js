const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 49 proof wires Lib.numbers.vec2 nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-49-vec2-proof.vuo"), "utf8");
  for (const title of ["my_AddVec2", "my_DampVec2", "my_DivideVector2", "my_DotVec2", "my_GridPosition", "my_HasVec2Changed", "my_Int2ToVector2", "my_PadVec2Range", "my_PerlinNoise2", "my_PickVector2", "my_RemapVec2", "my_ScaleVector2", "my_Vec2ToVec3", "my_Vector2Components", "my_Batch49Vec2Proof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-49-vec2-vuo-save/);
  assert.match(s, /ProofValue/);
});
