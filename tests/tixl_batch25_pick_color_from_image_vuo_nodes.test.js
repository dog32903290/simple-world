#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 25 Vuo node source preserves TiXL name, donor path, defaults, and color", () => {
  const source = read("vuo-nodes/my.numbers.color.pickColorFromImage.c");
  assert.match(source, /"title"\s*:\s*"my_PickColorFromImage"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/color\/PickColorFromImage\.cs/);
  assert.match(source, /Default: InputImage=null, Position=\(0\.0, 0\.0\), AlwaysUpdate=false/);
  assert.match(source, /Primary output: Vector4 Output \(ColorForValues #868C8D\)/);
});

test("Batch 25 Vuo node source preserves CPU readback, coordinate clamp, and cache behavior", () => {
  const source = read("vuo-nodes/my.numbers.color.pickColorFromImage.c");
  assert.match(source, /VuoImage_getBuffer/);
  assert.match(source, /GL_RGBA/);
  assert.match(source, /pickColumn/);
  assert.match(source, /pickRow/);
  assert.match(source, /clampIndex/);
  assert.match(source, /alwaysUpdate/);
  assert.match(source, /cachedPixels/);
  assert.match(source, /previousOutput/);
});

test("TiXL .t3 defaults support Batch 25 Vuo contract", () => {
  const t3 = read("external/tixl/Operators/Lib/numbers/color/PickColorFromImage.t3");
  assert.match(t3, /\/\*Position\*\/[\s\S]*"X": 0\.0[\s\S]*"Y": 0\.0/);
  assert.match(t3, /\/\*InputImage\*\/[\s\S]*"DefaultValue": null/);
  assert.match(t3, /\/\*AlwaysUpdate\*\/[\s\S]*"DefaultValue": false/);
});
