#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-32-basic-generate-proof.vuo");

test("Batch 32 proof wires basic image generators into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_CheckerBoard/);
  assert.match(source, /my_LinearGradient/);
  assert.match(source, /my_RadialGradient/);
  assert.match(source, /my_RoundedRect/);
  assert.match(source, /my_Batch32BasicGenerateProof/);
  assert.match(source, /CheckerBoard:textureOutput -> ProofImage:checkerImage/);
  assert.match(source, /LinearGradient:textureOutput -> ProofImage:linearImage/);
  assert.match(source, /RadialGradient:textureOutput -> ProofImage:radialImage/);
  assert.match(source, /RoundedRect:textureOutput -> ProofImage:roundedRectImage/);
  assert.match(source, /batch-32-basic-generate-vuo-save/);
});
