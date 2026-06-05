#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
test("Batch 36 proof wires Mandelbrot and MunchingSquares into a visible save path", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-36-fractal-generate-proof.vuo"), "utf8");
  assert.match(source, /my_MandelbrotFractal/);
  assert.match(source, /my_MunchingSquares2/);
  assert.match(source, /my_Batch36FractalGenerateProof/);
  assert.match(source, /batch-36-fractal-generate-vuo-save/);
});
