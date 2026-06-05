const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 52 Lib.numbers.vec3.process source namespace is audited", () => {
  for (const name of ["EaseVec3", "EaseVec3Keys", "SpringVec3"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec3/process/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/vec3/process/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
