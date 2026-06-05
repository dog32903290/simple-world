#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");
test("Batch 36 Vuo nodes preserve names, paths, texture color, and bounded limits", () => {
  const nodes = [
    ["vuo-nodes/my.image.generate.fractal.mandelbrotFractal.c", "my_MandelbrotFractal", "image/generate/fractal/MandelbrotFractal.cs", "MandelbrotFractal.hlsl"],
    ["vuo-nodes/my.image.generate.munchingSquares2.c", "my_MunchingSquares2", "image/generate/MunchingSquares2.cs", "MunchingSquares.hlsl"],
  ];
  for (const [file, title, donor, shader] of nodes) {
    const source = read(file);
    assert.match(source, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(source, new RegExp(`Source: external/tixl/Operators/Lib/${donor}`));
    assert.match(source, new RegExp(shader));
    assert.match(source, /Primary output: Texture2D TextureOutput \(ColorForTextures #9F008A\)/);
    assert.match(source, /Vuo body-layer limit/);
  }
});
