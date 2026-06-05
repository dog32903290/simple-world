#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-31-image-transform-proof.vuo");

test("Batch 31 proof wires image transform nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_Crop/);
  assert.match(source, /my_MakeTileableImage/);
  assert.match(source, /my_MirrorRepeat/);
  assert.match(source, /my_TransformImage/);
  assert.match(source, /my_Batch31ImageTransformProof/);
  assert.match(source, /Source:Image -> Crop:texture2d/);
  assert.match(source, /Source:Image -> MakeTileableImage:imageA/);
  assert.match(source, /Source:Image -> MirrorRepeat:image/);
  assert.match(source, /Source:Image -> TransformImage:image/);
  assert.match(source, /Crop:output -> ProofImage:cropImage/);
  assert.match(source, /MakeTileableImage:selected -> ProofImage:tileableImage/);
  assert.match(source, /MirrorRepeat:textureOutput -> ProofImage:mirrorImage/);
  assert.match(source, /TransformImage:textureOutput -> ProofImage:transformImage/);
  assert.match(source, /batch-31-image-transform-vuo-save/);
});
