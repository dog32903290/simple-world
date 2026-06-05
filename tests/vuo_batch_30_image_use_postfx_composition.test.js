#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-30-image-use-postfx-proof.vuo");

test("Batch 30 proof wires post-fx image-use nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_Fxaa/);
  assert.match(source, /my_NormalMap/);
  assert.match(source, /my_DepthBufferAsGrayScale/);
  assert.match(source, /my_Batch30ImageUsePostfxProof/);
  assert.match(source, /SourcePattern:Image -> Fxaa:image/);
  assert.match(source, /SourcePattern:Image -> NormalMap:lightMap/);
  assert.match(source, /SourceDepth:Image -> DepthBufferAsGrayScale:texture2d/);
  assert.match(source, /Fxaa:textureOutput -> ProofImage:fxaaImage/);
  assert.match(source, /NormalMap:output -> ProofImage:normalMapImage/);
  assert.match(source, /DepthBufferAsGrayScale:output -> ProofImage:depthImage/);
  assert.match(source, /batch-30-image-use-postfx-vuo-save/);
});
