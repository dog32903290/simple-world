#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const category = "Operators/Lib/numbers/float/logic";

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function inputPattern(type, name, defaultValue) {
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\"default\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

test("my_IsGreater preserves TiXL source, defaults, color, and strict comparison", () => {
  const source = read("vuo-nodes/my.numbers.float.logic.isGreater.c");
  assert.match(source, /"title"\s*:\s*"my_IsGreater"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/logic\/IsGreater\.cs/);
  assert.match(source, /Primary output: bool \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("VuoReal", "value", "1\\.0"));
  assert.match(source, inputPattern("VuoReal", "threshold", "0\\.5"));
  assert.match(source, /VuoOutputData\(VuoBoolean,\s*\{"name":"Result"\}\)\s*result/);
  assert.match(source, /\*result = value > threshold/);
});

test("my_PickFloat preserves TiXL source, defaults, color, and positive modulo index", () => {
  const source = read("vuo-nodes/my.numbers.float.logic.pickFloat.c");
  assert.match(source, /"title"\s*:\s*"my_PickFloat"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/logic\/PickFloat\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*floatValues/);
  assert.match(source, inputPattern("VuoInteger", "index", "0"));
  assert.match(source, /myPositiveMod/);
  assert.match(source, /VuoListGetValue_VuoReal\(floatValues, \(unsigned long\)wrappedIndex \+ 1\)/);
});

test("my_TryParse preserves TiXL source, defaults, color, and fallback output", () => {
  const source = read("vuo-nodes/my.numbers.float.logic.tryParse.c");
  assert.match(source, /"title"\s*:\s*"my_TryParse"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/logic\/TryParse\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoText,\s*\{"default":""\}\)\s*string/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0,\s*"name":"Default"\}\)\s*defaultValue/);
  assert.match(source, /strtod/);
});

test("my_ValueToRate preserves TiXL source, defaults, color, invariant newline parsing, and index selection", () => {
  const source = read("vuo-nodes/my.numbers.float.logic.valueToRate.c");
  assert.match(source, /"title"\s*:\s*"my_ValueToRate"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/logic\/ValueToRate\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("VuoReal", "value", "0\\.5"));
  assert.match(source, /VuoInputData\(VuoText,\s*\{"default":"0\\n0\.0625\\n0\.125\\n0\.25\\n0\.5\\n1\\n1\\n4\\n8\\n16\\n32"\}\)\s*rates/);
  assert.match(source, /myParseRatios/);
  assert.match(source, /\(count - 1\) \* clampedValue \+ 0\.5/);
});

test("TiXL .t3 defaults support Batch 6 float logic Vuo contracts", () => {
  const isGreaterT3 = read("external/tixl/Operators/Lib/numbers/float/logic/IsGreater.t3");
  const pickFloatT3 = read("external/tixl/Operators/Lib/numbers/float/logic/PickFloat.t3");
  const tryParseT3 = read("external/tixl/Operators/Lib/numbers/float/logic/TryParse.t3");
  const valueToRateT3 = read("external/tixl/Operators/Lib/numbers/float/logic/ValueToRate.t3");
  assert.match(isGreaterT3, /\/\*Value\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(isGreaterT3, /\/\*Threshold\*\/[\s\S]*"DefaultValue": 0\.5/);
  assert.match(pickFloatT3, /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(tryParseT3, /\/\*String\*\/[\s\S]*"DefaultValue": ""/);
  assert.match(tryParseT3, /\/\*Default\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(valueToRateT3, /\/\*Rates\*\/[\s\S]*"DefaultValue": "0\\n0\.0625\\n0\.125\\n0\.25\\n0\.5\\n1\\n1\\n4\\n8\\n16\\n32"/);
  assert.match(valueToRateT3, /\/\*Value\*\/[\s\S]*"DefaultValue": 0\.5/);
});
