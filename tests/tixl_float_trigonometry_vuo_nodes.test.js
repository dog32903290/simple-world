#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const category = "Operators/Lib/numbers/float/trigonometry";

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function inputPattern(name, defaultValue) {
  return new RegExp(`VuoInputData\\(VuoReal,\\s*\\{\\"default\\":${defaultValue}\\}\\)\\s*${name}\\b`);
}

test("my_Sin preserves TiXL source, defaults, color, and radians formula", () => {
  const source = read("vuo-nodes/my.numbers.float.trigonometry.sin.c");

  assert.match(source, /"title"\s*:\s*"my_Sin"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/trigonometry\/Sin\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("input", "0\\.0"));
  assert.match(source, inputPattern("period", "1\\.0"));
  assert.match(source, inputPattern("phase", "0\\.0"));
  assert.match(source, inputPattern("amplitude", "1\\.0"));
  assert.match(source, inputPattern("offset", "0\\.0"));
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result/);
  assert.match(source, /sin\(input \/ period \+ phase\) \* amplitude \+ offset/);
  assert.doesNotMatch(source, /VuoReal_makeFromDegrees|degrees/i);
});

test("my_Cos preserves TiXL source, defaults, color, and radians formula", () => {
  const source = read("vuo-nodes/my.numbers.float.trigonometry.cos.c");

  assert.match(source, /"title"\s*:\s*"my_Cos"/);
  assert.match(source, new RegExp(`Category: ${category}`));
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/trigonometry\/Cos\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, inputPattern("input", "0\\.0"));
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result/);
  assert.match(source, /cos\(input\)/);
  assert.doesNotMatch(source, /VuoReal_makeFromDegrees|degrees/i);
});

test("TiXL trigonometry source and .t3 defaults support the Vuo contracts", () => {
  const sinSource = read("external/tixl/Operators/Lib/numbers/float/trigonometry/Sin.cs");
  const cosSource = read("external/tixl/Operators/Lib/numbers/float/trigonometry/Cos.cs");
  const sinT3 = read("external/tixl/Operators/Lib/numbers/float/trigonometry/Sin.t3");
  const cosT3 = read("external/tixl/Operators/Lib/numbers/float/trigonometry/Cos.t3");

  assert.match(sinSource, /Math\.Sin\(Input\.GetValue\(context\) \/ Period\.GetValue\(context\) \+ Phase\.GetValue\(context\)\)/);
  assert.match(cosSource, /Math\.Cos\(Input\.GetValue\(context\)\)/);
  assert.match(sinT3, /\/\*Input\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(sinT3, /\/\*Period\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(sinT3, /\/\*Phase\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(sinT3, /\/\*Amplitude\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(sinT3, /\/\*Offset\*\/[\s\S]*"DefaultValue": 0\.0/);
  assert.match(cosT3, /\/\*Input\*\/[\s\S]*"DefaultValue": 0\.0/);
});
