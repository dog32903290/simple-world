const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 47 Lib.numbers.bool.process source namespace is audited", () => {
  for (const name of ["CacheBoolean", "DelayBoolean", "DelayTriggerChange", "KeepBoolean"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/bool/process/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/bool/process/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
