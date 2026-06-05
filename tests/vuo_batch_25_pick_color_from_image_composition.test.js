#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-25-pick-color-from-image-proof.vuo");

test("Batch 25 proof wires PickColorFromImage between a generated image and visible output", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_Batch25PickColorFromImageSource/);
  assert.match(source, /my_PickColorFromImage/);
  assert.match(source, /my_Batch25PickColorFromImageProof/);
  assert.match(source, /ImageSource:image -> PickColorFromImage:inputImage/);
  assert.match(source, /PickColorFromImage:output -> ProofImage:pickedColor/);
  assert.match(source, /ImageSource:image -> ProofImage:sourceImage/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 25 proof nodes expose generated image and picked color evidence", () => {
  const sourceNode = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch25PickColorFromImageSource.c"), "utf8");
  const proofNode = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch25PickColorFromImageProof.c"), "utf8");
  assert.match(sourceNode, /"title"\s*:\s*"my_Batch25PickColorFromImageSource"/);
  assert.match(sourceNode, /VuoOutputData\(VuoImage, \{"name":"Image"\}\)\s*image/);
  assert.match(proofNode, /"title"\s*:\s*"my_Batch25PickColorFromImageProof"/);
  assert.match(proofNode, /VuoInputData\(VuoImage\)\s*sourceImage/);
  assert.match(proofNode, /VuoInputData\(VuoColor\)\s*pickedColor/);
});
