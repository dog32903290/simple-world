const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 51 Lib.numbers.vec3 source namespace is audited", () => {
  for (const name of ["AddVec3", "BlendVector3", "CrossVec3", "DampVec3", "DotVec3", "EulerToAxisAngle", "HasVec3Changed", "LerpVec3", "Magnitude", "MulMatrix", "NormalizeVector3", "PerlinNoise3", "PickVector3", "RotateVector3", "RoundVec3", "ScaleVector3", "SubVec3", "TransformVec3", "Vec2Magnitude", "Vec3Distance", "Vector3Components", "Vector3Gizmo"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec3/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec3/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
