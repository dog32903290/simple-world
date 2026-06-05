const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 50 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.vec2.process.easeVec2.c", "my_EaseVec2", "EaseVec2"],
    ["vuo-nodes/my.numbers.vec2.process.easeVec2Keys.c", "my_EaseVec2Keys", "EaseVec2Keys"],
    ["vuo-nodes/my.numbers.vec2.process.springVec2.c", "my_SpringVec2", "SpringVec2"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/vec2/process/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/vec2\/process/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
