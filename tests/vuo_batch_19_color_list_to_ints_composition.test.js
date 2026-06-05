#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-19-color-list-to-ints-proof.vuo");

test("Batch 19 proof wires ColorListToInts into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_ColorsToList/);
  assert.match(source, /my_ColorListToInts/);
  assert.match(source, /my_Batch19ColorListToIntsProof/);
  assert.match(source, /ColorsToList:result -> ColorListToInts:colorList1/);
  assert.match(source, /ColorListToInts:result -> ProofImage:intList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 19 proof adapter exposes visible int-list inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch19ColorListToIntsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch19ColorListToIntsProof"/);
  assert.match(source, /VuoInputData\(VuoList_VuoInteger\)\s*intList/);
});
