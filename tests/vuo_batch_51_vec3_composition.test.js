const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 51 proof wires Lib.numbers.vec3 nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-51-vec3-proof.vuo"), "utf8");
  for (const title of ["my_AddVec3", "my_BlendVector3", "my_CrossVec3", "my_DampVec3", "my_DotVec3", "my_EulerToAxisAngle", "my_HasVec3Changed", "my_LerpVec3", "my_Magnitude", "my_MulMatrix", "my_NormalizeVector3", "my_PerlinNoise3", "my_PickVector3", "my_RotateVector3", "my_RoundVec3", "my_ScaleVector3", "my_SubVec3", "my_TransformVec3", "my_Vec2Magnitude", "my_Vec3Distance", "my_Vector3Components", "my_Vector3Gizmo", "my_Batch51Vec3Proof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-51-vec3-vuo-save/);
  assert.match(s, /ProofValue/);
});
