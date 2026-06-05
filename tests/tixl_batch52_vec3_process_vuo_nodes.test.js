const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 52 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.vec3.process.easeVec3.c", "my_EaseVec3", "EaseVec3"],
    ["vuo-nodes/my.numbers.vec3.process.easeVec3Keys.c", "my_EaseVec3Keys", "EaseVec3Keys"],
    ["vuo-nodes/my.numbers.vec3.process.springVec3.c", "my_SpringVec3", "SpringVec3"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/vec3/process/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/vec3\/process/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
