#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 16 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.floats.process.analyzeFloatList.c", "my_AnalyzeFloatList", "process/AnalyzeFloatList.cs", /Default: Input=\[5\.0,17\.0\]/],
    ["my.numbers.floats.process.sumRange.c", "my_SumRange", "process/SumRange.cs", /Default: Input=\[5\.0,17\.0\], LowerLimit=0, UpperLimit=999999/],
    ["my.numbers.floats.process.compareFloatLists.c", "my_CompareFloatLists", "process/CompareFloatLists.cs", /Default: ListA=\[\], ListB=\[\], Threshold=0\.0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/floats/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 16 Vuo node sources preserve process edge behavior", () => {
  const analyze = read("vuo-nodes/my.numbers.floats.process.analyzeFloatList.c");
  const sum = read("vuo-nodes/my.numbers.floats.process.sumRange.c");
  const compare = read("vuo-nodes/my.numbers.floats.process.compareFloatLists.c");

  assert.match(analyze, /isfinite/);
  assert.match(analyze, /sum \/ count/);
  assert.match(analyze, /NAN/);
  assert.match(sum, /previousSelected/);
  assert.match(sum, /lowerLimit < 0/);
  assert.match(sum, /upperLimit > count/);
  assert.match(compare, /TiXL-source bug/);
  assert.match(compare, /index >= countA \|\| index >= countB/);
  assert.match(compare, /fabs/);
});

test("TiXL .t3 defaults support Batch 16 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/AnalyzeFloatList.t3"), /\/\*Input\*\/[\s\S]*5\.0[\s\S]*17\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/SumRange.t3"), /\/\*UpperLimit\*\/[\s\S]*"DefaultValue": 999999/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/CompareFloatLists.t3"), /\/\*Threshold\*\/[\s\S]*"DefaultValue": 0\.0/);
});
