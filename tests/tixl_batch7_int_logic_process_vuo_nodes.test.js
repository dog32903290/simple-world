#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function inputPattern(type, name, defaultValue) {
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\"default\\":${defaultValue}`);
}

test("my_CompareInt preserves TiXL source, defaults, colors, mode clamp, and dual outputs", () => {
  const source = read("vuo-nodes/my.numbers.int.logic.compareInt.c");
  assert.match(source, /"title"\s*:\s*"my_CompareInt"/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/int\/logic/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/logic\/CompareInt\.cs/);
  assert.match(source, /Primary output: bool \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("VuoInteger", "mode", "1"));
  assert.match(source, /VuoOutputData\(VuoBoolean,\s*\{"name":"IsTrue"\}\)\s*isTrue/);
  assert.match(source, /VuoOutputData\(VuoInteger,\s*\{"name":"ResultValue"\}\)\s*resultValue/);
  assert.match(source, /myClampMode/);
});

test("my_PickInt preserves TiXL positive modulo list selection", () => {
  const source = read("vuo-nodes/my.numbers.int.logic.pickInt.c");
  assert.match(source, /"title"\s*:\s*"my_PickInt"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/logic\/PickInt\.cs/);
  assert.match(source, /VuoInputData\(VuoList_VuoInteger\)\s*inputValues/);
  assert.match(source, inputPattern("VuoInteger", "index", "0"));
  assert.match(source, /myPositiveMod/);
  assert.match(source, /VuoListGetValue_VuoInteger\(inputValues, \(unsigned long\)wrappedIndex \+ 1\)/);
});

test("my_ClampInt, my_FloatToInt, and my_GetAPrime preserve scalar process laws", () => {
  const clampSource = read("vuo-nodes/my.numbers.int.process.clampInt.c");
  const floatToIntSource = read("vuo-nodes/my.numbers.int.process.floatToInt.c");
  const primeSource = read("vuo-nodes/my.numbers.int.process.getAPrime.c");
  assert.match(clampSource, /"title"\s*:\s*"my_ClampInt"/);
  assert.match(clampSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/process\/ClampInt\.cs/);
  assert.match(clampSource, /myClampInt\(value, min, max\)/);
  assert.match(floatToIntSource, /"title"\s*:\s*"my_FloatToInt"/);
  assert.match(floatToIntSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/process\/FloatToInt\.cs/);
  assert.match(floatToIntSource, /\*integer = \(VuoInteger\)floatValue/);
  assert.match(primeSource, /"title"\s*:\s*"my_GetAPrime"/);
  assert.match(primeSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/process\/GetAPrime\.cs/);
  assert.match(primeSource, /myComputePrime/);
  assert.match(primeSource, /if \(index < 1\)/);
});

test("my_MaxInt and my_MinInt preserve TiXL multi-input empty-list sentinels", () => {
  const maxSource = read("vuo-nodes/my.numbers.int.process.maxInt.c");
  const minSource = read("vuo-nodes/my.numbers.int.process.minInt.c");
  assert.match(maxSource, /"title"\s*:\s*"my_MaxInt"/);
  assert.match(maxSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/process\/MaxInt\.cs/);
  assert.match(maxSource, /-2147483647 - 1/);
  assert.match(minSource, /"title"\s*:\s*"my_MinInt"/);
  assert.match(minSource, /Source: external\/tixl\/Operators\/Lib\/numbers\/int\/process\/MinInt\.cs/);
  assert.match(minSource, /2147483647/);
});

test("TiXL .t3 defaults support Batch 7 Vuo contracts", () => {
  const compareT3 = read("external/tixl/Operators/Lib/numbers/int/logic/CompareInt.t3");
  const pickT3 = read("external/tixl/Operators/Lib/numbers/int/logic/PickInt.t3");
  const clampT3 = read("external/tixl/Operators/Lib/numbers/int/process/ClampInt.t3");
  const floatToIntT3 = read("external/tixl/Operators/Lib/numbers/int/process/FloatToInt.t3");
  const primeT3 = read("external/tixl/Operators/Lib/numbers/int/process/GetAPrime.t3");
  assert.match(compareT3, /\/\*Mode\*\/[\s\S]*"DefaultValue": 1/);
  assert.match(compareT3, /\/\*ResultForTrue\*\/[\s\S]*"DefaultValue": 1/);
  assert.match(compareT3, /\/\*ResultForFalse\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(pickT3, /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(clampT3, /\/\*Min\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(floatToIntT3, /\/\*FloatValue\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(primeT3, /\/\*Index\*\/[\s\S]*"DefaultValue": 0/);
});
