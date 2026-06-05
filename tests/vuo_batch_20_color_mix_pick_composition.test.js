#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-20-color-mix-pick-proof.vuo");

test("Batch 20 proof wires blend and color-list picker nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_BlendColors/);
  assert.match(source, /my_PickColorFromList/);
  assert.match(source, /my_Batch20ColorMixPickProof/);
  assert.match(source, /BlendColors:color -> ProofImage:blendedColor/);
  assert.match(source, /PickColorFromList:selected -> ProofImage:pickedColor/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 20 proof adapter exposes visible color inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch20ColorMixPickProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch20ColorMixPickProof"/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*blendedColor/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*pickedColor/);
});
