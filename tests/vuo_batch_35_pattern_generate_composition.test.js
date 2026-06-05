#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-35-pattern-generate-proof.vuo");

test("Batch 35 proof wires all pattern nodes into a visible Vuo save path", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  for (const title of ["my_FraserGrid", "my_NumberPattern", "my_Raster", "my_Rings", "my_RyojiPattern1", "my_RyojiPattern2", "my_SinForm", "my_ValueRaster", "my_ZollnerPattern", "my_Batch35PatternGenerateProof"]) {
    assert.match(source, new RegExp(title));
  }
  assert.match(source, /batch-35-pattern-generate-vuo-save/);
  assert.match(source, /ProofImage:image -> SaveImage:saveImage/);
});
