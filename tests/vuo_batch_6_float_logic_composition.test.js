#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-6-float-logic-proof.vuo");

test("Batch 6 float logic proof wires all manufactured nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_IsGreater", "my_PickFloat", "my_TryParse", "my_ValueToRate"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch6FloatLogicProof/);
  assert.match(source, /IsGreater:result -> ProofImage:isGreaterValue/);
  assert.match(source, /PickFloat:selected -> ProofImage:pickFloatValue/);
  assert.match(source, /TryParse:result -> ProofImage:tryParseValue/);
  assert.match(source, /ValueToRate:result -> ProofImage:valueToRateValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /SaveImage \[type="vuo\.image\.save2"/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 6 proof adapter exposes one visual input per manufactured node output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch6FloatLogicProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch6FloatLogicProof"/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*isGreaterValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":8\.0\}\)\s*pickFloatValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":3\.25\}\)\s*tryParseValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.0\}\)\s*valueToRateValue/);
  assert.match(source, /float values\[4\]/);
});
