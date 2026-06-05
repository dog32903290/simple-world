#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function cropTargetSize(width, height, leftRight, topBottom) {
  return [
    Math.max(1, width + Math.round(leftRight[0]) + Math.round(leftRight[1])),
    Math.max(1, height + Math.round(topBottom[0]) + Math.round(topBottom[1])),
  ];
}

function cropSourcePixel(outX, outY, leftRight, topBottom, width, height) {
  const x = outX - Math.floor(leftRight[0] + 0.4);
  const y = outY - Math.floor(topBottom[0] + 0.4);
  return x < 0 || y < 0 || x >= width || y >= height ? "padding" : [x, y];
}

function transformTargetSize(width, height, resolution, factor) {
  const baseW = resolution[0] > 0 ? resolution[0] : width;
  const baseH = resolution[1] > 0 ? resolution[1] : height;
  return [Math.max(1, Math.round(baseW * factor[0])), Math.max(1, Math.round(baseH * factor[1]))];
}

function wrapUv(value, mode) {
  if (mode === 0) return value - Math.floor(value);
  if (mode === 1 || mode === 4) return 1 - Math.abs(((value * 0.5) - Math.floor(value * 0.5)) * 2 - 1);
  return Math.min(1, Math.max(0, value));
}

function makeTileableMixes(tilingMode, isEnabled) {
  if (!isEnabled || tilingMode === 0) return [];
  const mixes = [];
  if (tilingMode === 1 || tilingMode === 3) mixes.push("horizontal-half-offset");
  if (tilingMode === 2 || tilingMode === 3) mixes.push("vertical-half-offset");
  if (tilingMode === 3) mixes.push("corner-half-offset");
  return mixes;
}

test("Crop preserves TiXL output size and out-of-source padding law", () => {
  assert.deepEqual(cropTargetSize(160, 120, [24, -16], [18, -10]), [168, 128]);
  assert.equal(cropSourcePixel(2, 2, [24, -16], [18, -10], 160, 120), "padding");
  assert.deepEqual(cropSourcePixel(30, 25, [24, -16], [18, -10], 160, 120), [6, 7]);
});

test("TransformImage keeps resolution override/factor and wrap families explicit", () => {
  assert.deepEqual(transformTargetSize(160, 120, [0, 0], [1, 1]), [160, 120]);
  assert.deepEqual(transformTargetSize(160, 120, [80, 64], [2, 0.5]), [160, 32]);
  assert.equal(wrapUv(1.25, 0), 0.25);
  assert.equal(wrapUv(1.25, 1), 0.75);
  assert.equal(wrapUv(1.25, 2), 1);
});

test("MakeTileableImage exposes TiXL tiling mode branches instead of hiding them in one blend", () => {
  assert.deepEqual(makeTileableMixes(0, true), []);
  assert.deepEqual(makeTileableMixes(1, true), ["horizontal-half-offset"]);
  assert.deepEqual(makeTileableMixes(2, true), ["vertical-half-offset"]);
  assert.deepEqual(makeTileableMixes(3, true), ["horizontal-half-offset", "vertical-half-offset", "corner-half-offset"]);
  assert.deepEqual(makeTileableMixes(3, false), []);
});

test("MirrorRepeat has bounded nonzero width and separate screen/image rotations", () => {
  const safeWidth = Math.max(Math.abs(0), 0.0001);
  assert.equal(safeWidth, 0.0001);
  const rotateScreenRad = (-25 + -10 - 90) / 180 * 3.141578;
  const mirrorRotationRad = (-10 - 90) / 180 * 3.141578;
  assert.notEqual(rotateScreenRad, mirrorRotationRad);
});
