const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 49 Lib.numbers.vec2 source namespace is audited", () => {
  for (const name of ["AddVec2", "DampVec2", "DivideVector2", "DotVec2", "GridPosition", "HasVec2Changed", "Int2ToVector2", "PadVec2Range", "PerlinNoise2", "PickVector2", "RemapVec2", "ScaleVector2", "Vec2ToVec3", "Vector2Components"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec2/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec2/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
