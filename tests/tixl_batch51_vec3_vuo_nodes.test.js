const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 51 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.vec3.addVec3.c", "my_AddVec3", "AddVec3"],
    ["vuo-nodes/my.numbers.vec3.blendVector3.c", "my_BlendVector3", "BlendVector3"],
    ["vuo-nodes/my.numbers.vec3.crossVec3.c", "my_CrossVec3", "CrossVec3"],
    ["vuo-nodes/my.numbers.vec3.dampVec3.c", "my_DampVec3", "DampVec3"],
    ["vuo-nodes/my.numbers.vec3.dotVec3.c", "my_DotVec3", "DotVec3"],
    ["vuo-nodes/my.numbers.vec3.eulerToAxisAngle.c", "my_EulerToAxisAngle", "EulerToAxisAngle"],
    ["vuo-nodes/my.numbers.vec3.hasVec3Changed.c", "my_HasVec3Changed", "HasVec3Changed"],
    ["vuo-nodes/my.numbers.vec3.lerpVec3.c", "my_LerpVec3", "LerpVec3"],
    ["vuo-nodes/my.numbers.vec3.magnitude.c", "my_Magnitude", "Magnitude"],
    ["vuo-nodes/my.numbers.vec3.mulMatrix.c", "my_MulMatrix", "MulMatrix"],
    ["vuo-nodes/my.numbers.vec3.normalizeVector3.c", "my_NormalizeVector3", "NormalizeVector3"],
    ["vuo-nodes/my.numbers.vec3.perlinNoise3.c", "my_PerlinNoise3", "PerlinNoise3"],
    ["vuo-nodes/my.numbers.vec3.pickVector3.c", "my_PickVector3", "PickVector3"],
    ["vuo-nodes/my.numbers.vec3.rotateVector3.c", "my_RotateVector3", "RotateVector3"],
    ["vuo-nodes/my.numbers.vec3.roundVec3.c", "my_RoundVec3", "RoundVec3"],
    ["vuo-nodes/my.numbers.vec3.scaleVector3.c", "my_ScaleVector3", "ScaleVector3"],
    ["vuo-nodes/my.numbers.vec3.subVec3.c", "my_SubVec3", "SubVec3"],
    ["vuo-nodes/my.numbers.vec3.transformVec3.c", "my_TransformVec3", "TransformVec3"],
    ["vuo-nodes/my.numbers.vec3.vec2Magnitude.c", "my_Vec2Magnitude", "Vec2Magnitude"],
    ["vuo-nodes/my.numbers.vec3.vec3Distance.c", "my_Vec3Distance", "Vec3Distance"],
    ["vuo-nodes/my.numbers.vec3.vector3Components.c", "my_Vector3Components", "Vector3Components"],
    ["vuo-nodes/my.numbers.vec3.vector3Gizmo.c", "my_Vector3Gizmo", "Vector3Gizmo"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/vec3/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/vec3/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
