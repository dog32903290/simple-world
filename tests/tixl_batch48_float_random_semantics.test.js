const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 48 Lib.numbers.float.random source namespace is audited", () => {
  for (const name of ["FloatHash", "PerlinNoise", "Random"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/float/random/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/float/random/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
