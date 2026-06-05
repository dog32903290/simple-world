#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-1-grade-a-numbers-proof.vuo");

test("Batch 1 Grade A numbers proof wires every manufactured node into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  const requiredNodes = [
    "my_SmoothStep",
    "my_Sin",
    "my_Cos",
    "my_IntAdd",
    "my_SubInts",
    "my_MultiplyInt",
    "my_IntDiv",
    "my_ModInt",
    "my_IntToFloat",
    "my_IsIntEven",
  ];

  for (const title of requiredNodes) {
    assert.match(source, new RegExp(title));
  }

  assert.match(source, /my_Batch1GradeANumbersProof/);
  assert.match(source, /vuo\.image\.render\.window2/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /FireOnStart:started -> SmoothStep:value/);
  assert.match(source, /Sin:result -> ProofImage:sinValue/);
  assert.match(source, /IsIntEven:result -> ProofImage:isIntEvenValue/);
});
