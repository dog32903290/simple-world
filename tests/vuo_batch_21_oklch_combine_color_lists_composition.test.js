#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-21-oklch-combine-color-lists-proof.vuo");

test("Batch 21 proof wires OKLCh and CombineColorLists into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_OKLChToColor/);
  assert.match(source, /my_CombineColorLists/);
  assert.match(source, /my_Batch21OklchCombineColorListsProof/);
  assert.match(source, /OKLChToColor:color -> ColorsToListA:color1/);
  assert.match(source, /CombineColorLists:selected -> ProofImage:combinedList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 21 proof adapter exposes visible color-list inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch21OklchCombineColorListsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch21OklchCombineColorListsProof"/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*oklchColor/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*combinedList/);
});
