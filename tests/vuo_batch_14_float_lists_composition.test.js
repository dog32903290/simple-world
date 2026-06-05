#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-14-float-lists-proof.vuo");

test("Batch 14 proof wires all manufactured float-list nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_FloatsToList", "my_FloatListLength", "my_SetFloatListValue", "my_PickFloatFromList"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch14FloatListsProof/);
  assert.match(source, /FloatsToList:result -> PickFloatFromList:input/);
  assert.match(source, /FloatsToList:result -> ProofImage:floatsToList/);
  assert.match(source, /PickFloatFromList:selected -> ProofImage:pickedValue/);
  assert.match(source, /FloatListLength:length -> ProofImage:lengthValue/);
  assert.match(source, /SetFloatListValue:result -> ProofImage:setList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 14 proof adapter exposes visible float-list result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch14FloatListsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch14FloatListsProof"/);
  assert.match(source, /VuoInputData\(VuoInteger,\s*\{"default":3\}\)\s*lengthValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":30\.0\}\)\s*pickedValue/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*floatsToList/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*setList/);
});
