#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-29-image-use-material-channels-proof.vuo");

test("Batch 29 proof wires material channel image-use nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_CombineMaterialChannels2/);
  assert.match(source, /my_CombineMaterialChannels/);
  assert.match(source, /my_Batch29ImageUseMaterialChannelsProof/);
  assert.match(source, /SourceA:Image -> CombineMaterialChannels2:imageA/);
  assert.match(source, /Roughness:Image -> CombineMaterialChannels:roughness/);
  assert.match(source, /CombineMaterialChannels2:output -> ProofImage:combineMaterialChannels2Image/);
  assert.match(source, /CombineMaterialChannels:output -> ProofImage:combineMaterialChannelsImage/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
  assert.match(source, /batch-29-image-use-material-channels-vuo-save/);
});
