#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-11-bool-aggregate-proof.vuo");

test("Batch 11 proof wires All and Any into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_All/);
  assert.match(source, /my_Any/);
  assert.match(source, /my_Batch11BoolAggregateProof/);
  assert.match(source, /All:result -> ProofImage:allValue/);
  assert.match(source, /Any:result -> ProofImage:anyValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 11 proof adapter exposes visible bool result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch11BoolAggregateProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch11BoolAggregateProof"/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*allValue/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*anyValue/);
});
