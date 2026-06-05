#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("my_Sigmoid preserves TiXL source, defaults, color, and exact exponent sign", () => {
  const source = read("vuo-nodes/my.numbers.float.adjust.sigmoid.c");
  assert.match(source, /"title"\s*:\s*"my_Sigmoid"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/adjust\/Sigmoid\.cs/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/adjust/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*value\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*stretch\b/);
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result\b/);
  assert.match(source, /1\.0 \/ \(1\.0 \+ pow\(M_E,\s*stretch \* value\)\)/);
});

test("my_Log preserves TiXL source, defaults, color, and Math.Log base behavior", () => {
  const source = read("vuo-nodes/my.numbers.float.basic.log.c");
  assert.match(source, /"title"\s*:\s*"my_Log"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/basic\/Log\.cs/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/basic/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*value\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*base\b/);
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result\b/);
  assert.match(source, /base == 1\.0/);
  assert.match(source, /value == 1\.0 && base != 1\.0/);
  assert.match(source, /base <= 0\.0/);
  assert.match(source, /value < 0\.0/);
  assert.match(source, /log\(value\) \/ log\(base\)/);
  assert.match(source, /adapter-bounded.*NaN/i);
});

test("my_Compare preserves TiXL source, defaults, color, mode clamp, and precision behavior", () => {
  const source = read("vuo-nodes/my.numbers.float.logic.compare.c");
  assert.match(source, /"title"\s*:\s*"my_Compare"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/logic\/Compare\.cs/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/logic/);
  assert.match(source, /Primary output: bool \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*value\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*testValue\b/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":1,\s*"suggestedMin":0,\s*"suggestedMax":3\}\)\s*mode\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.001\}\)\s*precision\b/);
  assert.match(source, /VuoOutputData\(VuoBoolean,\s*\{"name":"IsTrue"\}\)\s*isTrue\b/);
  assert.match(source, /myClampMode/);
  assert.match(source, /fabs\(value - testValue\) < precision/);
  assert.match(source, /fabs\(value - testValue\) >= precision/);
});

test("TiXL .t3 defaults support Batch 5 scalar Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/float/adjust/Sigmoid.t3"), /\/\*Value\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/adjust/Sigmoid.t3"), /\/\*Stretch\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/basic/Log.t3"), /\/\*Base\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/basic/Log.t3"), /\/\*Value\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/logic/Compare.t3"), /\/\*Mode\*\/[\s\S]*"DefaultValue": 1/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/logic/Compare.t3"), /\/\*Precision\*\/[\s\S]*"DefaultValue": 0\.001/);
});
