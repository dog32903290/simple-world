#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-22-gradient-core-proof.vuo");

test("Batch 22 proof wires gradient builders and sampler into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_BuildGradient/);
  assert.match(source, /my_DefineGradient/);
  assert.match(source, /my_SampleGradient/);
  assert.match(source, /my_Batch22GradientCoreProof/);
  assert.match(source, /BuildGradient:gradientColors -> SampleBuilt:gradientColors/);
  assert.match(source, /DefineGradient:gradientColors -> SampleDefined:gradientColors/);
  assert.match(source, /SampleBuilt:color -> ProofImage:builtSampleColor/);
  assert.match(source, /SampleDefined:color -> ProofImage:definedSampleColor/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 22 proof adapter exposes visible gradient sample inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch22GradientCoreProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch22GradientCoreProof"/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*builtSampleColor/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*definedSampleColor/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*builtGradientColors/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*builtGradientPositions/);
});
