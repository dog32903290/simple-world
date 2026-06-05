const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 49 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.vec2.addVec2.c", "my_AddVec2", "AddVec2"],
    ["vuo-nodes/my.numbers.vec2.dampVec2.c", "my_DampVec2", "DampVec2"],
    ["vuo-nodes/my.numbers.vec2.divideVector2.c", "my_DivideVector2", "DivideVector2"],
    ["vuo-nodes/my.numbers.vec2.dotVec2.c", "my_DotVec2", "DotVec2"],
    ["vuo-nodes/my.numbers.vec2.gridPosition.c", "my_GridPosition", "GridPosition"],
    ["vuo-nodes/my.numbers.vec2.hasVec2Changed.c", "my_HasVec2Changed", "HasVec2Changed"],
    ["vuo-nodes/my.numbers.vec2.int2ToVector2.c", "my_Int2ToVector2", "Int2ToVector2"],
    ["vuo-nodes/my.numbers.vec2.padVec2Range.c", "my_PadVec2Range", "PadVec2Range"],
    ["vuo-nodes/my.numbers.vec2.perlinNoise2.c", "my_PerlinNoise2", "PerlinNoise2"],
    ["vuo-nodes/my.numbers.vec2.pickVector2.c", "my_PickVector2", "PickVector2"],
    ["vuo-nodes/my.numbers.vec2.remapVec2.c", "my_RemapVec2", "RemapVec2"],
    ["vuo-nodes/my.numbers.vec2.scaleVector2.c", "my_ScaleVector2", "ScaleVector2"],
    ["vuo-nodes/my.numbers.vec2.vec2ToVec3.c", "my_Vec2ToVec3", "Vec2ToVec3"],
    ["vuo-nodes/my.numbers.vec2.vector2Components.c", "my_Vector2Components", "Vector2Components"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/vec2/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/vec2/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
