#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-9-int2-proof.vuo");

test("Batch 9 proof wires all manufactured Int2 nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of [
    "my_AddInt2",
    "my_Int2Components",
    "my_MakeResolution",
    "my_MaxInt2",
    "my_ScaleResolution",
    "my_ScaleSize",
  ]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch9Int2Proof/);
  assert.match(source, /AddInt2:result -> ProofImage:addInt2Value/);
  assert.match(source, /Int2Components:width -> ProofImage:componentWidth/);
  assert.match(source, /MakeResolution:size -> ProofImage:makeResolutionValue/);
  assert.match(source, /MaxInt2:maxSize -> ProofImage:maxInt2Value/);
  assert.match(source, /ScaleResolution:size -> ProofImage:scaleResolutionValue/);
  assert.match(source, /ScaleSize:result -> ProofImage:scaleSizeValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 9 proof adapter exposes one visual input per manufactured output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch9Int2Proof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch9Int2Proof"/);
  assert.match(source, /VuoInputData\(VuoPoint2d,\s*\{"default":\{"x":384\.0,"y":216\.0\}\}\)\s*addInt2Value/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":640\}\)\s*componentWidth/);
  assert.match(source, /VuoInputData\(VuoPoint2d,\s*\{"default":\{"x":960\.0,"y":540\.0\}\}\)\s*maxInt2Value/);
  assert.match(source, /float values\[8\]/);
});
