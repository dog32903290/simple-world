#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 14 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.floats.basic.floatsToList.c", "my_FloatsToList", "basic/FloatsToList.cs", /Default: Input=0\.0/],
    ["my.numbers.floats.basic.floatListLength.c", "my_FloatListLength", "basic/FloatListLength.cs", /Default: Input=\[\]/],
    ["my.numbers.floats.basic.setFloatListValue.c", "my_SetFloatListValue", "basic/SetFloatListValue.cs", /Default: Mode=0, TriggerSet=false, FloatList=\[\], Index=0, Value=0\.0/],
    ["my.numbers.floats.logic.pickFloatFromList.c", "my_PickFloatFromList", "logic/PickFloatFromList.cs", /Default: Input=\[5\.0,17\.0\], Index=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/floats/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 14 Vuo node sources preserve float list edge behavior", () => {
  const pick = read("vuo-nodes/my.numbers.floats.logic.pickFloatFromList.c");
  const set = read("vuo-nodes/my.numbers.floats.basic.setFloatListValue.c");
  const length = read("vuo-nodes/my.numbers.floats.basic.floatListLength.c");
  const make = read("vuo-nodes/my.numbers.floats.basic.floatsToList.c");

  assert.match(make, /VuoListCreate_VuoReal/);
  assert.match(length, /input \? VuoListGetCount_VuoReal\(input\) : 0/);
  assert.match(pick, /positiveMod/);
  assert.match(pick, /count == 0[\s\S]*\*selected = 0\.0/);
  assert.match(set, /if \(!triggerSet/);
  assert.match(set, /previousResult/);
  assert.match(set, /index == -2/);
  assert.match(set, /mode == 1[\s\S]*current \+ value/);
  assert.match(set, /mode == 2[\s\S]*current \* value/);
});

test("TiXL .t3 defaults support Batch 14 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/basic/FloatsToList.t3"), /\/\*Input\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/basic/FloatListLength.t3"), /\/\*Input\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/basic/SetFloatListValue.t3"), /\/\*TriggerSet\*\/[\s\S]*"DefaultValue": false/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/logic/PickFloatFromList.t3"), /\/\*Input\*\/[\s\S]*5\.0[\s\S]*17\.0/);
});
