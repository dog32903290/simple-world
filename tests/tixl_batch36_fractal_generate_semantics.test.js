#!/usr/bin/env node
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");
test("Batch 36 TiXL generator sources preserve procedural contracts", () => {
  assert.match(read("external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.cs"), /sealed class MandelbrotFractal/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.t3"), /\/\*Scale\*\/[\s\S]*"DefaultValue": -0\.5/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs"), /sealed class MunchingSquares2/);
  assert.match(read("external/tixl/Operators/Lib/image/generate/MunchingSquares2.cs"), /Classic[\s\S]*Patterns[\s\S]*Chaos/);
  assert.ok(fs.existsSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl")));
  assert.ok(fs.existsSync(path.join(repoRoot, "external/tixl/Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl")));
});
