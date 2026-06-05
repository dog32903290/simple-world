#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-28-image-use-blend-combine-proof.vuo");

test("Batch 28 proof wires blend/combine image-use nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_BlendImages/);
  assert.match(source, /my_BlendWithMask/);
  assert.match(source, /my_Combine3Images/);
  assert.match(source, /my_Batch28ImageUseBlendCombineProof/);
  assert.match(source, /SourceA:Image -> BlendImages:input1/);
  assert.match(source, /BlendImages:outputImage -> ProofImage:blendImagesImage/);
  assert.match(source, /BlendWithMask:output -> ProofImage:blendWithMaskImage/);
  assert.match(source, /Combine3Images:output -> ProofImage:combine3ImagesImage/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
  assert.match(source, /batch-28-image-use-blend-combine-vuo-save/);
});
