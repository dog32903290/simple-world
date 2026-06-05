#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-10-ints-proof.vuo");

test("Batch 10 proof wires all manufactured ints nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_IntListLength", "my_IntsToList", "my_MergeIntLists", "my_PickIntFromList", "my_SetIntListValue"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch10IntsProof/);
  assert.match(source, /IntListLength:length -> ProofImage:lengthValue/);
  assert.match(source, /PickIntFromList:selected -> ProofImage:pickedValue/);
  assert.match(source, /MergeIntLists:result -> IntListLength:input/);
  assert.match(source, /SetIntListValue:result -> ProofImage:setList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 10 proof adapter exposes visible controls for each manufactured output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch10IntsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch10IntsProof"/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":6\}\)\s*lengthValue/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":30\}\)\s*pickedValue/);
  assert.match(source, /VuoInputData\(VuoList_VuoInteger\)\s*mergedList/);
  assert.match(source, /VuoInputData\(VuoList_VuoInteger\)\s*setList/);
});
