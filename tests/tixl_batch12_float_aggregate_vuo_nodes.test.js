#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 12 Vuo node sources preserve TiXL names, donors, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.float.basic.sum.c", "my_Sum", "float/basic/Sum.cs", /Default: InputValues=0\.0/],
    ["my.numbers.float.process.blendValues.c", "my_BlendValues", "float/process/BlendValues.cs", /Default: Values=0\.0, F=0\.0/],
    ["my.numbers.float.process.remapValues.c", "my_RemapValues", "float/process/RemapValues.cs", /Default: InputAndOutputPairs=\(0,0\), InputValue=0\.0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: float Result \(ColorForValues #868C8D\)/);
    assert.match(source, defaults);
  }
});

test("Batch 12 Vuo node sources preserve float aggregate edge behavior", () => {
  const sumSource = read("vuo-nodes/my.numbers.float.basic.sum.c");
  const blendSource = read("vuo-nodes/my.numbers.float.process.blendValues.c");
  const remapSource = read("vuo-nodes/my.numbers.float.process.remapValues.c");
  assert.match(sumSource, /count == 0[\s\S]*\*result = defaultValue/);
  assert.match(blendSource, /myFmod/);
  assert.match(blendSource, /floor\(value \/ mod\)/);
  assert.match(blendSource, /index1/);
  assert.match(remapSource, /minDistance = INFINITY/);
  assert.match(remapSource, /distance < minDistance/);
  assert.match(remapSource, /bestIndex == -1[\s\S]*\*result = 0\.0/);
});

test("TiXL .t3 defaults support Batch 12 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/float/basic/Sum.t3"), /\/\*InputValues\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/process/BlendValues.t3"), /\/\*F\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/process/RemapValues.t3"), /\/\*InputAndOutputPairs\*\/[\s\S]*"X": 0\.0[\s\S]*"Y": 0\.0/);
});
