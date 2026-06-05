const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 48 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.float.random.floatHash.c", "my_FloatHash", "FloatHash"],
    ["vuo-nodes/my.numbers.float.random.perlinNoise.c", "my_PerlinNoise", "PerlinNoise"],
    ["vuo-nodes/my.numbers.float.random.random.c", "my_Random", "Random"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/float/random/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/float\/random/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
