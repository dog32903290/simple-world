#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-23-gradient-pick-blend-proof.vuo");

test("Batch 23 proof wires PickGradient and BlendGradients into visible sampled color outputs", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_PickGradient/);
  assert.match(source, /my_BlendGradients/);
  assert.match(source, /my_SampleGradient Picked/);
  assert.match(source, /my_SampleGradient Blended/);
  assert.match(source, /my_Batch23GradientPickBlendProof/);
  assert.match(source, /PickGradient:selectedColors -> SamplePicked:gradientColors/);
  assert.match(source, /BlendGradients:resultColors -> SampleBlended:gradientColors/);
  assert.match(source, /SamplePicked:color -> ProofImage:pickedSampleColor/);
  assert.match(source, /SampleBlended:color -> ProofImage:blendedSampleColor/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 23 proof adapter exposes picked and blended gradient evidence", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch23GradientPickBlendProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch23GradientPickBlendProof"/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*pickedSampleColor/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*blendedSampleColor/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*pickedGradientColors/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*blendedGradientColors/);
});
