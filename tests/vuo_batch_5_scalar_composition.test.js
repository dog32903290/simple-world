#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-5-scalar-proof.vuo");

test("Batch 5 scalar proof wires Sigmoid, Log, and Compare into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_Sigmoid", "my_Log", "my_Compare"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch5ScalarProof/);
  assert.match(source, /Sigmoid:result -> ProofImage:sigmoidValue/);
  assert.match(source, /Log:result -> ProofImage:logValue/);
  assert.match(source, /Compare:isTrue -> ProofImage:compareValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /SaveImage \[type="vuo\.image\.save2"/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 5 proof adapter exposes one visual input per manufactured node output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch5ScalarProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch5ScalarProof"/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.5\}\)\s*sigmoidValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":3\.0\}\)\s*logValue/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*compareValue/);
  assert.match(source, /float values\[3\]/);
});
