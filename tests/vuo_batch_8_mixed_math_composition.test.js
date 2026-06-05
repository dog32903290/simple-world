#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-8-mixed-math-proof.vuo");

test("Batch 8 proof wires all manufactured nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_Atan2", "my_AddInts", "my_MultiplyInts", "my_SumInts"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch8MixedMathProof/);
  assert.match(source, /Atan2:result -> ProofImage:atan2Value/);
  assert.match(source, /AddInts:result -> ProofImage:addIntsValue/);
  assert.match(source, /MultiplyInts:result -> ProofImage:multiplyIntsValue/);
  assert.match(source, /SumInts:result -> ProofImage:sumIntsValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 8 proof adapter exposes one visual input per manufactured output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch8MixedMathProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch8MixedMathProof"/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":1\.57079632679\}\)\s*atan2Value/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":7\}\)\s*addIntsValue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":24\}\)\s*multiplyIntsValue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":3\}\)\s*sumIntsValue/);
  assert.match(source, /float values\[4\]/);
});
