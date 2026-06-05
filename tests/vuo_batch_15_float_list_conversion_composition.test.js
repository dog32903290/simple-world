#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-15-float-list-conversion-proof.vuo");

test("Batch 15 proof wires conversion and float-list picker nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_IntListToFloatList", "my_FloatListToIntList", "my_PickFloatList"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch15FloatListConversionProof/);
  assert.match(source, /IntListToFloatList:result -> ProofImage:intToFloatList/);
  assert.match(source, /FloatListToIntList:result -> ProofImage:floatToIntList/);
  assert.match(source, /PickFloatList:selected -> ProofImage:pickedList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 15 proof adapter exposes visible list result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch15FloatListConversionProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch15FloatListConversionProof"/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*intToFloatList/);
  assert.match(source, /VuoInputData\(VuoList_VuoInteger\)\s*floatToIntList/);
  assert.match(source, /VuoInputData\(VuoList_VuoReal\)\s*pickedList/);
});
