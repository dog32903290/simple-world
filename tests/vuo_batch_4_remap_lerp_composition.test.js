#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-4-remap-lerp-proof.vuo");

test("Batch 4 Remap/Lerp proof wires corrected my_ nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  assert.match(source, /my_Remap/);
  assert.match(source, /my_Lerp/);
  assert.match(source, /my_Batch4RemapLerpProof/);
  assert.doesNotMatch(source, /TiXL Remap/);
  assert.doesNotMatch(source, /TiXL Lerp/);
  assert.match(source, /Remap:result -> ProofImage:remapValue/);
  assert.match(source, /Lerp:result -> ProofImage:lerpValue/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
});

test("Batch 4 proof adapter exposes Remap and Lerp result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.remapLerpProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch4RemapLerpProof"/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":12\.5\}\)\s*remapValue/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":20\.0\}\)\s*lerpValue/);
  assert.match(source, /float values\[2\]/);
});
