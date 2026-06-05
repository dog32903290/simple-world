#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-26-gradients-to-texture-proof.vuo");

test("Batch 26 proof wires GradientsToTexture into Vuo image output and save", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_BuildGradient/);
  assert.match(source, /my_DefineGradient/);
  assert.match(source, /my_GradientsToTexture/);
  assert.match(source, /BuildGradient:gradientColors -> GradientsToTexture:gradient1Colors/);
  assert.match(source, /DefineGradient:gradientColors -> GradientsToTexture:gradient2Colors/);
  assert.match(source, /GradientsToTexture:gradientsTexture -> RenderWindow:image/);
  assert.match(source, /GradientsToTexture:gradientsTexture -> SaveImage:saveImage/);
});
