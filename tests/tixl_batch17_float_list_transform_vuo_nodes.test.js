#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 17 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.floats.process.combineFloatLists.c", "my_CombineFloatLists", "process/CombineFloatLists.cs", /Default: InputLists=\[\]/],
    ["my.numbers.floats.process.remapFloatList.c", "my_RemapFloatList", "process/RemapFloatList.cs", /Default: FloatList=\[\], RangeInMin=0\.0, RangeInMax=1\.0, RangeOutMin=0\.0, RangeOutMax=1\.0, BiasAndGain=\(0\.5,0\.5\), Mode=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/floats/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 17 Vuo node sources preserve list-transform edge behavior", () => {
  const combine = read("vuo-nodes/my.numbers.floats.process.combineFloatLists.c");
  const remap = read("vuo-nodes/my.numbers.floats.process.remapFloatList.c");

  assert.match(combine, /Vuo bounded adapter: fixed 3 input lists/);
  assert.match(combine, /inputCount/);
  assert.match(combine, /VuoListAppendValue_VuoReal/);
  assert.match(remap, /myApplyGainAndBias/);
  assert.match(remap, /fabs\(inRange\) < 0\.00001/);
  assert.match(remap, /min \+ myFmod\(v - min, modRange\)/);
});

test("TiXL .t3 defaults support Batch 17 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/CombineFloatLists.t3"), /\/\*InputLists\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.t3"), /\/\*BiasAndGain\*\/[\s\S]*"X": 0\.5[\s\S]*"Y": 0\.5/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/RemapFloatList.t3"), /\/\*RangeOutMax\*\/[\s\S]*"DefaultValue": 1\.0/);
});
