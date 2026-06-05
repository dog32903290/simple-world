#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-13-dict-selectors-proof.vuo");

test("Batch 13 proof wires all dict selector nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_SelectFloatFromDict", "my_SelectBoolFromFloatDict", "my_SelectVec2FromDict", "my_SelectVec3FromDict"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /my_Batch13DictSelectorsProof/);
  assert.match(source, /SelectFloat:result -> ProofImage:floatValue/);
  assert.match(source, /SelectBool:result -> ProofImage:boolValue/);
  assert.match(source, /SelectVec2:result -> ProofImage:vec2Value/);
  assert.match(source, /SelectVec3:result -> ProofImage:vec3Value/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});

test("Batch 13 proof adapter exposes visible dict selector result inputs", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.batch13DictSelectorsProof.c"), "utf8");
  assert.match(source, /"title"\s*:\s*"my_Batch13DictSelectorsProof"/);
  assert.match(source, /VuoInputData\(VuoReal,\s*\{"default":0\.75\}\)\s*floatValue/);
  assert.match(source, /VuoInputData\(VuoBoolean,\s*\{"default":true\}\)\s*boolValue/);
  assert.match(source, /VuoInputData\(VuoPoint2d,\s*\{"default":\{"x":1\.0,"y":2\.0\}\}\)\s*vec2Value/);
  assert.match(source, /VuoInputData\(VuoPoint3d,\s*\{"default":\{"x":10\.0,"y":20\.0,"z":30\.0\}\}\)\s*vec3Value/);
});
