#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("my_Atan2 preserves TiXL source, defaults, color, and Vector component order", () => {
  const source = read("vuo-nodes/my.numbers.float.trigonometry.atan2.c");
  assert.match(source, /"title"\s*:\s*"my_Atan2"/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/trigonometry/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/trigonometry\/Atan2\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoPoint2d,\s*\{"default":\{"x":0\.0,"y":0\.0\}\}\)\s*vector/);
  assert.match(source, /atan2\(vector\.x, vector\.y\)/);
  assert.doesNotMatch(source, /atan2\(vector\.y, vector\.x\)/);
});

test("my_AddInts, my_MultiplyInts, and my_SumInts preserve TiXL integer aggregate laws", () => {
  const addSource = read("vuo-nodes/my.numbers.int.basic.addInts.c");
  const multiplySource = read("vuo-nodes/my.numbers.int.basic.multiplyInts.c");
  const sumSource = read("vuo-nodes/my.numbers.int.basic.sumInts.c");
  assert.match(addSource, /"title"\s*:\s*"my_AddInts"/);
  assert.match(addSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/basic\/AddInts\.cs/);
  assert.match(addSource, /input1 \+ input2/);
  assert.match(multiplySource, /"title"\s*:\s*"my_MultiplyInts"/);
  assert.match(multiplySource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/basic\/MultiplyInts\.cs/);
  assert.match(multiplySource, /count == 0/);
  assert.match(multiplySource, /\*result = count == 0 \? 0 : total/);
  assert.match(sumSource, /"title"\s*:\s*"my_SumInts"/);
  assert.match(sumSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/basic\/SumInts\.cs/);
  assert.match(sumSource, /defaultValue/);
  assert.match(sumSource, /\*result = count == 0 \? defaultValue : total/);
});

test("TiXL .t3 defaults support Batch 8 Vuo contracts", () => {
  const atanT3 = read("external/tixl/Operators/Lib/numbers/float/trigonometry/Atan2.t3");
  const addT3 = read("external/tixl/Operators/Lib/numbers/int/basic/AddInts.t3");
  const multiplyT3 = read("external/tixl/Operators/Lib/numbers/int/basic/MultiplyInts.t3");
  const sumT3 = read("external/tixl/Operators/Lib/numbers/int/basic/SumInts.t3");
  assert.match(atanT3, /"X": 0\.0/);
  assert.match(atanT3, /"Y": 0\.0/);
  assert.match(addT3, /\/\*Input1\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(addT3, /\/\*Input2\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(multiplyT3, /\/\*InputValues\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(sumT3, /\/\*InputValues\*\/[\s\S]*"DefaultValue": 0/);
});
