#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 24 Vuo node source preserves TiXL name, donor path, defaults, and color", () => {
  const source = read("vuo-nodes/my.numbers.color.keepColors.c");
  assert.match(source, /"title"\s*:\s*"my_KeepColors"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/color\/KeepColors\.cs/);
  assert.match(source, /Default: Color=white, AddColorToList=true, MaxLength=100, Reset=false/);
  assert.match(source, /Primary output: List<Vector4> Result \(ColorForValues #868C8D\)/);
});

test("Batch 24 Vuo node source preserves stateful insert, reset, and clamp behavior", () => {
  const source = read("vuo-nodes/my.numbers.color.keepColors.c");
  assert.match(source, /nodeInstanceInit/);
  assert.match(source, /nodeInstanceEvent/);
  assert.match(source, /prependColor/);
  assert.match(source, /trimToLength/);
  assert.match(source, /clampLength/);
  assert.match(source, /reset/);
  assert.match(source, /addColorToList/);
});

test("TiXL .t3 defaults support Batch 24 Vuo contract", () => {
  const t3 = read("external/tixl/Operators/Lib/numbers/color/KeepColors.t3");
  assert.match(t3, /\/\*AddColorToList\*\/[\s\S]*"DefaultValue": true/);
  assert.match(t3, /\/\*MaxLength\*\/[\s\S]*"DefaultValue": 100/);
  assert.match(t3, /\/\*Reset\*\/[\s\S]*"DefaultValue": false/);
  assert.match(t3, /\/\*Color\*\/[\s\S]*"X": 1\.0[\s\S]*"W": 1\.0/);
});
