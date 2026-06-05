#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-7-int-logic-process-proof.vuo");

test("Batch 7 int proof wires all manufactured nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_CompareInt", "my_PickInt", "my_ClampInt", "my_FloatToInt", "my_GetAPrime", "my_MaxInt", "my_MinInt"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch7IntLogicProcessProof/);
  assert.match(source, /CompareInt:isTrue -> ProofImage:compareIsTrue/);
  assert.match(source, /CompareInt:resultValue -> ProofImage:compareResultValue/);
  assert.match(source, /PickInt:selected -> ProofImage:pickIntValue/);
  assert.match(source, /ClampInt:result -> ProofImage:clampIntValue/);
  assert.match(source, /FloatToInt:integer -> ProofImage:floatToIntValue/);
  assert.match(source, /GetAPrime:result -> ProofImage:primeValue/);
  assert.match(source, /MaxInt:result -> ProofImage:maxIntValue/);
  assert.match(source, /MinInt:result -> ProofImage:minIntValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 7 proof adapter exposes one visual input per manufactured output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch7IntLogicProcessProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch7IntLogicProcessProof"/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*compareIsTrue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":10\}\)\s*compareResultValue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":33\}\)\s*pickIntValue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":10\}\)\s*clampIntValue/);
  assert.match(source, /float values\[8\]/);
});
