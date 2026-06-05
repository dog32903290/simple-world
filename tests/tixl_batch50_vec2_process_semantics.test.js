const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 50 Lib.numbers.vec2.process source namespace is audited", () => {
  for (const name of ["EaseVec2", "EaseVec2Keys", "SpringVec2"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec2/process/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec2/process/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
