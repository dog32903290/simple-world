#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("my_Remap replaces the legacy TiXL Remap body with exact TiXL naming, metadata, ports, and mode behavior", () => {
  const source = read("vuo-nodes/my.numbers.float.adjust.remap.c");
  assert.match(source, /"title"\s*:\s*"my_Remap"/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/adjust/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/adjust\/Remap\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*value\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*rangeInMin\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*rangeInMax\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*rangeOutMin\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*rangeOutMax\b/);
  assert.match(source, /VuoInputData\(VuoPoint2d,\s*\{"default":\{"x":0\.5,"y":0\.5\}\}\)\s*biasAndGain\b/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":0,\s*"suggestedMin":0,\s*"suggestedMax":2\}\)\s*mode\b/);
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result\b/);
  assert.match(source, /myApplyGainAndBias/);
  assert.match(source, /mode == 1/);
  assert.match(source, /mode == 2/);
  assert.match(source, /myFmod\(v,\s*max - min\)/);
  assert.doesNotMatch(source, /"title"\s*:\s*"TiXL Remap"/);
});

test("my_Lerp replaces the legacy TiXL Lerp body with exact TiXL naming, metadata, ports, and clamp behavior", () => {
  const source = read("vuo-nodes/my.numbers.float.process.lerp.c");
  assert.match(source, /"title"\s*:\s*"my_Lerp"/);
  assert.match(source, /Category: Operators\/Lib\/numbers\/float\/process/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/float\/process\/Lerp\.cs/);
  assert.match(source, /Primary output: float \(ColorForValues #868C8D\)/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*a\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*b\b/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.0\}\)\s*f\b/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":false\}\)\s*clamp\b/);
  assert.match(source, /VuoOutputData\(VuoReal,\s*\{"name":"Result"\}\)\s*result\b/);
  assert.match(source, /factor = myClamp\(factor,\s*0\.0,\s*1\.0\)/);
  assert.match(source, /a \+ \(b - a\) \* factor/);
  assert.doesNotMatch(source, /"title"\s*:\s*"TiXL Lerp"/);
});

test("TiXL Remap and Lerp source defaults support the corrected Vuo contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/numbers/float/adjust/Remap.t3"), /"DefaultValue": \{\s*"X": 0\.5,\s*"Y": 0\.5\s*\}/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/adjust/Remap.t3"), /"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/process/Lerp.t3"), /\/\*B\*\/[\s\S]*"DefaultValue": 1\.0/);
  assert.match(read("external/tixl/Operators/Lib/numbers/float/process/Lerp.t3"), /\/\*Clamp\*\/[\s\S]*"DefaultValue": false/);
});
