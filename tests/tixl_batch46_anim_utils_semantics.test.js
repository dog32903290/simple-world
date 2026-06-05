const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 46 Lib.numbers.anim.utils source namespace is audited", () => {
  for (const name of ["FindKeyframes", "SetKeyframes"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/utils/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/utils/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
