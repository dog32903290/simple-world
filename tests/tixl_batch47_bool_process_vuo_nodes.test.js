const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 47 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.bool.process.cacheBoolean.c", "my_CacheBoolean", "CacheBoolean"],
    ["vuo-nodes/my.numbers.bool.process.delayBoolean.c", "my_DelayBoolean", "DelayBoolean"],
    ["vuo-nodes/my.numbers.bool.process.delayTriggerChange.c", "my_DelayTriggerChange", "DelayTriggerChange"],
    ["vuo-nodes/my.numbers.bool.process.keepBoolean.c", "my_KeepBoolean", "KeepBoolean"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/bool/process/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/bool\/process/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
