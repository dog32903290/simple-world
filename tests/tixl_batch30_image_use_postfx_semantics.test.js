#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function depthToGray(depth, near = 0.01, far = 1000, outputRange = [0, 5], clampOutput = false, mode = 0) {
  if (depth < 0) return "checker";
  let c = mode < 0.5
    ? (-far * near) / (depth * (far - near) - far)
    : (2 * near) / (far + near - depth * (far - near));
  if (outputRange[0] !== 0 || outputRange[1] !== 0) {
    c = (c - outputRange[0]) / (outputRange[1] - outputRange[0]);
  }
  return clampOutput ? Math.min(1, Math.max(0, c)) : c;
}

function normalMapFlat(mode = 0) {
  if (mode < 0.5) return [0.5, 0.5, 1, 1];
  if (mode < 1.5) return [0, 0, 1, 1];
  if (mode < 2.5) return [0, 0, 0, 1];
  return [0.5, 0.5, 0, 1];
}

function fxaaAlpha(inputAlpha, keepAlpha) {
  return keepAlpha ? inputAlpha : 1;
}

test("DepthBufferAsGrayScale preserves TiXL linearization, output remap, clamp, and negative-depth checker sentinel", () => {
  assert.equal(depthToGray(-1), "checker");
  assert.ok(Math.abs(depthToGray(0.5) - 0.003999960000399996) < 1e-9);
  assert.equal(depthToGray(0.5, 0.01, 1000, [0, 5], true), 0.003999960000399996);
  assert.equal(depthToGray(10, 0.01, 1000, [0, 5], true), 0);
});

test("NormalMap flat input follows TiXL mode output families", () => {
  assert.deepEqual(normalMapFlat(0), [0.5, 0.5, 1, 1]);
  assert.deepEqual(normalMapFlat(1), [0, 0, 1, 1]);
  assert.deepEqual(normalMapFlat(2), [0, 0, 0, 1]);
  assert.deepEqual(normalMapFlat(3), [0.5, 0.5, 0, 1]);
});

test("Fxaa preserves alpha only when KeepAlpha is true", () => {
  assert.equal(fxaaAlpha(0.25, true), 0.25);
  assert.equal(fxaaAlpha(0.25, false), 1);
});
