#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 11 Vuo node sources preserve TiXL All/Any names, donors, defaults, and colors", () => {
  const nodes = [
    ["my.numbers.bool.combine.all.c", "my_All", "All.cs"],
    ["my.numbers.bool.combine.any.c", "my_Any", "Any.cs"],
  ];

  for (const [file, title, donor] of nodes) {
    const source = read(`vuo-nodes/${file}`);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/numbers/bool/combine/${donor.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
    assert.match(source, /Category: Operators\/Lib\/numbers\/bool\/combine/);
    assert.match(source, /Primary output: bool Result \(ColorForValues #868C8D\)/);
    assert.match(source, /Default: Input=false/);
  }
});

test("Batch 11 Vuo node sources preserve empty-input edge behavior", () => {
  const allSource = read("vuo-nodes/my.numbers.bool.combine.all.c");
  const anySource = read("vuo-nodes/my.numbers.bool.combine.any.c");
  assert.match(allSource, /count == 0[\s\S]*\*result = false/);
  assert.match(allSource, /for \(unsigned long i = 1; i <= count; \+\+i\)/);
  assert.match(anySource, /VuoListGetCount_VuoBoolean/);
  assert.match(anySource, /if \(VuoListGetValue_VuoBoolean\(inputValues, i\)\)/);
});

test("TiXL .t3 defaults support Batch 11 Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/bool/combine/All.t3"), /\/\*Input\*\/[\s\S]*"DefaultValue": false/);
  assert.match(read("external/tixl/Operators/Lib/numbers/bool/combine/Any.t3"), /\/\*Input\*\/[\s\S]*"DefaultValue": false/);
});
