#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-17-float-list-transform-proof.vuo");

test("Batch 17 proof wires float-list transform nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_CombineFloatLists", "my_RemapFloatList"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch17FloatListTransformProof/);
  assert.match(source, /CombineFloatLists:selected -> ProofImage:combinedList/);
  assert.match(source, /RemapFloatList:result -> ProofImage:remappedList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 17 proof adapter exposes visible list-transform inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch17FloatListTransformProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch17FloatListTransformProof"/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*combinedList/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*remappedList/);
});
