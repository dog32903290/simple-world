#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 15 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.floats.conversion.intListToFloatList.c", "my_IntListToFloatList", "conversion/IntListToFloatList.cs", /Default: IntList=\[\]/],
    ["my.numbers.floats.conversion.floatListToIntList.c", "my_FloatListToIntList", "conversion/FloatListToIntList.cs", /Default: FloatList=\[\]/],
    ["my.numbers.floats.logic.pickFloatList.c", "my_PickFloatList", "logic/PickFloatList.cs", /Default: Input=\[\], Index=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/floats/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 15 Vuo node sources preserve conversion and picker edge behavior", () => {
  const intToFloat = read("vuo-nodes/my.numbers.floats.conversion.intListToFloatList.c");
  const floatToInt = read("vuo-nodes/my.numbers.floats.conversion.floatListToIntList.c");
  const pickList = read("vuo-nodes/my.numbers.floats.logic.pickFloatList.c");

  assert.match(intToFloat, /VuoListCreate_VuoReal/);
  assert.match(intToFloat, /VuoListAppendValue_VuoReal/);
  assert.match(floatToInt, /\(VuoInteger\)VuoListGetValue_VuoReal/);
  assert.match(pickList, /Vuo bounded adapter: fixed 3 input lists/);
  assert.match(pickList, /positiveMod/);
  assert.match(pickList, /inputCount <= 0[\s\S]*previousResult/);
});

test("TiXL .t3 defaults support Batch 15 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/conversion/IntListToFloatList.t3"), /\/\*IntList\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/conversion/FloatListToIntList.t3"), /\/\*FloatList\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/logic/PickFloatList.t3"), /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
});
