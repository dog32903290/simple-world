#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 9 Vuo node titles, TiXL donor paths, defaults, and Int2 mapping are explicit", () => {
  const nodes = [
    ["my.numbers.int2.basic.addInt2.c", "my_AddInt2", "basic/AddInt2.cs", /Input1=\(0,0\), Input2=\(0,0\)/],
    ["my.numbers.int2.process.int2Components.c", "my_Int2Components", "process/Int2Components.cs", /Resolution=\(0,0\)/],
    ["my.numbers.int2.process.makeResolution.c", "my_MakeResolution", "process/MakeResolution.cs", /Width=0, Height=0/],
    ["my.numbers.int2.process.maxInt2.c", "my_MaxInt2", "process/MaxInt2.cs", /Sizes=\(0,0\)/],
    ["my.numbers.int2.process.scaleResolution.c", "my_ScaleResolution", "process/ScaleResolution.cs", /Resolution=\(0,0\), Factor=\(0,0\), ClampToValidTextureSize=false/],
    ["my.numbers.int2.process.scaleSize.c", "my_ScaleSize", "process/ScaleSize.cs", /InputSize=\(0,0\), Stretch=\(1,1\), Scale=1/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/int2/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, /TiXL Int2 is carried as VuoPoint2d/);
    assert.match(source, defaults);
  }
});

test("Batch 9 Vuo nodes preserve TiXL integer component behavior", () => {
  const add = read("vuo-nodes/my.numbers.int2.basic.addInt2.c");
  const components = read("vuo-nodes/my.numbers.int2.process.int2Components.c");
  const max = read("vuo-nodes/my.numbers.int2.process.maxInt2.c");
  const scaleResolution = read("vuo-nodes/my.numbers.int2.process.scaleResolution.c");
  const scaleSize = read("vuo-nodes/my.numbers.int2.process.scaleSize.c");

  assert.match(add, /toInt\(input1\.x\) \+ toInt\(input2\.x\)/);
  assert.match(components, /\*length = w \* h/);
  assert.match(components, /\*aspectRatio = \(VuoReal\)w \/ \(VuoReal\)h/);
  assert.match(max, /VuoInteger maxWidth = 0/);
  assert.match(max, /VuoListGetCount_VuoPoint2d/);
  assert.match(scaleResolution, /maxSize = 16384/);
  assert.match(scaleResolution, /value <= 0[\s\S]*return 1/);
  assert.match(scaleSize, /toInt\(inputSize\.x\) \* scale \* stretch\.x/);
});

test("TiXL .t3 defaults support Batch 9 Vuo contracts", () => {
  const files = [
    "external/tixl/Operators/Lib/numbers/int2/basic/AddInt2.t3",
    "external/tixl/Operators/Lib/numbers/int2/process/Int2Components.t3",
    "external/tixl/Operators/Lib/numbers/int2/process/MakeResolution.t3",
    "external/tixl/Operators/Lib/numbers/int2/process/MaxInt2.t3",
    "external/tixl/Operators/Lib/numbers/int2/process/ScaleResolution.t3",
    "external/tixl/Operators/Lib/numbers/int2/process/ScaleSize.t3",
  ];

  for (const file of files) {
    const source = read(file);
    assert.match(source, /"DefaultValue"/);
  }

  assert.match(read(files[4]), /\/\*ClampToValidTextureSize\*\/[\s\S]*"DefaultValue": false/);
  assert.match(read(files[5]), /\/\*Stretch\*\/[\s\S]*"X": 1\.0[\s\S]*"Y": 1\.0/);
});
