const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 53 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.vec4.dotVec4.c", "my_DotVec4", "DotVec4"],
    ["vuo-nodes/my.numbers.vec4.pickColor.c", "my_PickColor", "PickColor"],
    ["vuo-nodes/my.numbers.vec4.rgbaToColor.c", "my_RgbaToColor", "RgbaToColor"],
    ["vuo-nodes/my.numbers.vec4.vector4Components.c", "my_Vector4Components", "Vector4Components"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/vec4/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/vec4/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
