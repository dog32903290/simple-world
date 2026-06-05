const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 54 Lib.numbers.anim.animators source namespace is audited", () => {
  for (const name of ["AnimBoolean", "AnimFloatList", "AnimInt", "AnimValue", "AnimVec2", "AnimVec3", "OscillateVec2", "OscillateVec3", "SequenceAnim", "TriggerAnim"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/animators/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/animators/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
