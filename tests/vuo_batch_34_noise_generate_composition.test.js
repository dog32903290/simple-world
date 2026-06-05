#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-34-noise-generate-proof.vuo");

test("Batch 34 proof wires all image generate noise nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_FractalNoise", "my_Grain", "my_ShardNoise", "my_TileableNoise", "my_WorleyNoise", "my_Batch34NoiseGenerateProof"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /FractalNoise:textureOutput -> ProofImage:fractalImage/);
  assert.match(source, /Grain:textureOutput -> ProofImage:grainImage/);
  assert.match(source, /ShardNoise:textureOutput -> ProofImage:shardImage/);
  assert.match(source, /TileableNoise:textureOutput -> ProofImage:tileableImage/);
  assert.match(source, /WorleyNoise:textureOutput -> ProofImage:worleyImage/);
  assert.match(source, /batch-34-noise-generate-vuo-save/);
});
