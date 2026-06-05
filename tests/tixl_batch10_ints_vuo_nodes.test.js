#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 10 Vuo node sources preserve TiXL names, donor paths, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.ints.intListLength.c", "my_IntListLength", "IntListLength.cs", /Input=\[\]/],
    ["my.numbers.ints.intsToList.c", "my_IntsToList", "IntsToList.cs", /Input=0/],
    ["my.numbers.ints.mergeIntLists.c", "my_MergeIntLists", "MergeIntLists.cs", /Enabled=false, MaxSize=-1, MergeMode=0/],
    ["my.numbers.ints.pickIntFromList.c", "my_PickIntFromList", "PickIntFromList.cs", /Input=\[\], Index=0/],
    ["my.numbers.ints.setIntListValue.c", "my_SetIntListValue", "SetIntListValue.cs", /Mode=0, TriggerSet=false, IntList=\[\], Index=0, Value=0/],
  ];

  for (const [file, title, donor, defaults] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/ints/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Primary output: .*ColorForValues #868C8D/);
    assert.match(source, defaults);
  }
});

test("Batch 10 Vuo node sources preserve list edge behavior", () => {
  const pick = read("vuo-nodes/my.numbers.ints.pickIntFromList.c");
  const merge = read("vuo-nodes/my.numbers.ints.mergeIntLists.c");
  const set = read("vuo-nodes/my.numbers.ints.setIntListValue.c");
  assert.match(pick, /positiveMod/);
  assert.match(pick, /count == 0[\s\S]*\*selected = 0/);
  assert.match(merge, /modeAppend/);
  assert.match(merge, /modeHtp/);
  assert.match(merge, /modeLtp/);
  assert.match(merge, /modeFailOver/);
  assert.match(merge, /modeAverage/);
  assert.match(merge, /Vuo bounded adapter: fixed 3 input lists/);
  assert.match(set, /if \(!triggerSet/);
  assert.match(set, /index == -2/);
});

test("TiXL .t3 defaults support Batch 10 Vuo contracts", () => {
  for (const file of [
    "IntListLength.t3",
    "IntsToList.t3",
    "MergeIntLists.t3",
    "PickIntFromList.t3",
    "SetIntListValue.t3",
  ]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/ints/${file}`), /"DefaultValue"/);
  }
  assert.match(read("external/tixl/Operators/Lib/numbers/ints/MergeIntLists.t3"), /\/\*MaxSize\*\/[\s\S]*"DefaultValue": -1/);
  assert.match(read("external/tixl/Operators/Lib/numbers/ints/SetIntListValue.t3"), /\/\*TriggerSet\*\/[\s\S]*"DefaultValue": false/);
});
