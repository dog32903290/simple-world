#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-16-float-list-process-proof.vuo");

test("Batch 16 proof wires float-list process nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_AnalyzeFloatList", "my_SumRange", "my_CompareFloatLists"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch16FloatListProcessProof/);
  assert.match(source, /AnalyzeFloatList:min -> ProofImage:minValue/);
  assert.match(source, /AnalyzeFloatList:max -> ProofImage:maxValue/);
  assert.match(source, /AnalyzeFloatList:averageMean -> ProofImage:averageMean/);
  assert.match(source, /AnalyzeFloatList:allValid -> ProofImage:allValid/);
  assert.match(source, /SumRange:selected -> ProofImage:sumRangeSelected/);
  assert.match(source, /CompareFloatLists:difference -> ProofImage:compareDifference/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 16 proof adapter exposes visible process result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch16FloatListProcessProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch16FloatListProcessProof"/);
  assert.match(source, /VuoInputData\(VuoReal\)\s*minValue/);
  assert.match(source, /VuoInputData\(VuoReal\)\s*maxValue/);
  assert.match(source, /VuoInputData\(VuoReal\)\s*averageMean/);
  assert.match(source, /VuoInputData\(VuoBoolean\)\s*allValid/);
  assert.match(source, /VuoInputData\(VuoReal\)\s*sumRangeSelected/);
  assert.match(source, /VuoInputData\(VuoReal\)\s*compareDifference/);
});
