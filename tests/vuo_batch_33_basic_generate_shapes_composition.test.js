#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-33-basic-generate-shapes-proof.vuo");

test("Batch 33 proof wires remaining basic image generators into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_Blob/);
  assert.match(source, /my_BoxGradient/);
  assert.match(source, /my_NGon/);
  assert.match(source, /my_NGonGradient/);
  assert.match(source, /my_Batch33BasicGenerateShapesProof/);
  assert.match(source, /Blob:textureOutput -> ProofImage:blobImage/);
  assert.match(source, /BoxGradient:textureOutput -> ProofImage:boxGradientImage/);
  assert.match(source, /NGon:textureOutput -> ProofImage:nGonImage/);
  assert.match(source, /NGonGradient:textureOutput -> ProofImage:nGonGradientImage/);
  assert.match(source, /batch-33-basic-generate-shapes-vuo-save/);
});
