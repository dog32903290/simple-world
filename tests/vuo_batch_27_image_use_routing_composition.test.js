#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-27-image-use-routing-proof.vuo");

test("Batch 27 proof wires all image/use routing nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_FirstValidTexture/);
  assert.match(source, /my_PickTexture/);
  assert.match(source, /my_SwapTextures/);
  assert.match(source, /my_UseFallbackTexture/);
  assert.match(source, /my_Batch27ImageUseRoutingProof/);
  assert.match(source, /SourceA:Image -> FirstValidTexture:input2/);
  assert.match(source, /FirstValidTexture:output -> ProofImage:firstValidImage/);
  assert.match(source, /PickTexture:selected -> ProofImage:pickedImage/);
  assert.match(source, /SwapTextures:textureA -> ProofImage:swapAImage/);
  assert.match(source, /SwapTextures:textureB -> ProofImage:swapBImage/);
  assert.match(source, /UseFallbackTexture:output -> ProofImage:fallbackImage/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
  assert.match(source, /batch-27-image-use-routing-vuo-save/);
});
