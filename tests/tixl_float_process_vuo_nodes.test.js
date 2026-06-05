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
  return new RegExp(`VuoInputData\\(${type},\\s*\\{\\"default\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

test("my_SmoothStep preserves TiXL source, defaults, color, and smootherstep formula", () => {
  const source = read("vuo-nodes/my.numbers.float.process.smoothStep.c");

  assert.match(source, /"title"\s*:\s*"my_SmoothStep"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/process\/SmoothStep\.cs/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/process/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("VuoReal", "min", "0\\.0"));
  assert.match(source, inputPattern("VuoReal", "max", "1\\.0"));
  assert.match(source, inputPattern("VuoReal", "value", "1\\.0"));
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result/);
  assert.match(source, /mySmootherStep/);
  assert.match(source, /t \* t \* t \* \(t \* \(t \* 6\.0 - 15\.0\) \+ 10\.0\)/);
  assert.doesNotMatch(source, /t \* t \* \(3\.0 - 2\.0 \* t\)/);
});

test("TiXL SmoothStep source and .t3 defaults support the Vuo contract", () => {
  const csharp = read("external/tixl/Operators/Lib/numbers/float/process/SmoothStep.cs");
  const t3 = read("external/tixl/Operators/Lib/numbers/float/process/SmoothStep.t3");

  assert.match(csharp, /MathUtils\.SmootherStep\(Min\.GetValue\(context\),\s*Max\.GetValue\(context\),\s*Value\.GetValue\(context\)\)/);
  assert.match(t3, /\/\*Min\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(t3, /\/\*Max\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(t3, /\/\*Value\*\/[\s\S]*"DefaultValue": 1\.0/);
});
