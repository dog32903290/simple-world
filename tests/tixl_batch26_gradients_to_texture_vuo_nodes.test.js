#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

test("Batch 26 Vuo node source preserves TiXL name, donor path, defaults, and texture color", () => {
  const source = read("vuo-nodes/my.numbers.color.gradientsToTexture.c");
  assert.match(source, /"title"\s*:\s*"my_GradientsToTexture"/);
  assert.match(source, /Source: external\/tixl\/Operators\/Lib\/numbers\/color\/GradientsToTexture\.cs/);
  assert.match(source, /Default: Resolution=256, Direction=0/);
  assert.match(source, /Primary output: Texture2D GradientsTexture \(ColorForTextures #9F008A\)/);
});

test("Batch 26 Vuo node source preserves gradient texture layout and bounded adapter", () => {
  const source = read("vuo-nodes/my.numbers.color.gradientsToTexture.c");
  assert.match(source, /Vuo bounded adapter: TiXL Gradient maps to color list \+ position list \+ interpolation enum/);
  assert.match(source, /sampleGradientColor/);
  assert.match(source, /resolution/);
  assert.match(source, /direction/);
  assert.match(source, /useHorizontal/);
  assert.match(source, /VuoImage_makeFromBuffer/);
  assert.match(source, /VuoImageColorDepth_8/);
});

test("TiXL .t3 defaults support Batch 26 Vuo contract", () => {
  const t3 = read("external/tixl/Operators/Lib/numbers/color/GradientsToTexture.t3");
  assert.match(t3, /\/\*Resolution\*\/[\s\S]*"DefaultValue": 256/);
  assert.match(t3, /\/\*Direction\*\/[\s\S]*"DefaultValue": 0/);
  assert.match(t3, /\/\*Gradients\*\/[\s\S]*"Interpolation": "Linear"/);
});
