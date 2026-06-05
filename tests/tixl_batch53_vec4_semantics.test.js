const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 53 Lib.numbers.vec4 source namespace is audited", () => {
  for (const name of ["DotVec4", "PickColor", "RgbaToColor", "Vector4Components"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec4/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec4/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
