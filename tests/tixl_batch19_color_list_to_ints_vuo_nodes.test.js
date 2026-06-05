#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 19 Vuo node source preserves TiXL name, donor path, defaults, and color", () => {
  const source = read("vuo-nodes/my.numbers.floats.process.colorListToInts.c");
  assert.match(source, /"title"\s*:\s*"my_ColorListToInts"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/floats\/process\/ColorListToInts\.cs/);
  assert.match(source, /Default: ColorLists=\[\], OutputMode=0/);
  assert.match(source, /Primary output: List<int> Result \(ColorForValues #868C8D\)/);
});

test("Batch 19 Vuo node source preserves output modes and channel conversion", () => {
  const source = read("vuo-nodes/my.numbers.floats.process.colorListToInts.c");
  assert.match(source, /Vuo bounded adapter: fixed 3 color-list inputs/);
  assert.match(source, /appendChannelValues/);
  assert.match(source, /outputMode == 0/);
  assert.match(source, /outputMode == 4/);
  assert.match(source, /\(VuoInteger\)clampReal\(value \* 255\.0, 0\.0, 255\.0\)/);
  assert.match(source, /VuoListAppendValue_VuoInteger/);
});

test("TiXL .t3 defaults support Batch 19 Vuo contract", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.t3"), /\/\*ColorLists\*\/[\s\S]*"Values": \[\]/);
  assert.match(read("external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.t3"), /\/\*OutputMode\*\/[\s\S]*"DefaultValue": 0/);
});
