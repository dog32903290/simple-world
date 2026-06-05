#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-24-keep-colors-proof.vuo");

test("Batch 24 proof wires KeepColors into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_KeepColors/);
  assert.match(source, /my_Batch24KeepColorsProof/);
  assert.match(source, /DisplayRefresh:requestedFrame -> KeepColors:update/);
  assert.match(source, /KeepColors:result -> ProofImage:keptColors/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 24 proof adapter exposes visible kept color list input", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch24KeepColorsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch24KeepColorsProof"/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*keptColors/);
  assert.match(source, /keptColorCount/);
});
