#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-18-color-values-proof.vuo");

test("Batch 18 proof wires color value nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_HSBToColor", "my_HSLToColor", "my_ColorsToList"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch18ColorValuesProof/);
  assert.match(source, /HSBToColor:color -> ProofImage:hsbColor/);
  assert.match(source, /HSLToColor:color -> ProofImage:hslColor/);
  assert.match(source, /ColorsToList:result -> ProofImage:colorList/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 18 proof adapter exposes visible color inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch18ColorValuesProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch18ColorValuesProof"/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*hsbColor/);
  assert.match(source, /VuoInputData\(VuoColor\)\s*hslColor/);
  assert.match(source, /VuoInputData\(VuoList_VuoColor\)\s*colorList/);
});
