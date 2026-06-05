#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-12-float-aggregate-proof.vuo");

test("Batch 12 proof wires all manufactured float aggregate nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_Sum", "my_BlendValues", "my_RemapValues"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch12FloatAggregateProof/);
  assert.match(source, /Sum:result -> ProofImage:sumValue/);
  assert.match(source, /BlendValues:result -> ProofImage:blendValue/);
  assert.match(source, /RemapValues:result -> ProofImage:remapValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 12 proof adapter exposes visible float result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch12FloatAggregateProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch12FloatAggregateProof"/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":2\.75\}\)\s*sumValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":32\.5\}\)\s*blendValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":50\.0\}\)\s*remapValue/);
});
