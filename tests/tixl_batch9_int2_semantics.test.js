#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function int2(x, y) {
  return { x, y };
}

function trunc(value) {
  return value < 0 ? Math.ceil(value) : Math.floor(value);
}

function addInt2(input1, input2) {
  return int2(input1.x + input2.x, input1.y + input2.y);
}

function int2Components(resolution) {
  return {
    width: resolution.x,
    height: resolution.y,
    length: resolution.x * resolution.y,
    aspectRatio: resolution.x / resolution.y,
  };
}

function makeResolution(width, height) {
  return int2(width, height);
}

function maxInt2(sizes) {
  return sizes.reduce(
    (maxSize, size) => int2(Math.max(maxSize.x, size.x), Math.max(maxSize.y, size.y)),
    int2(0, 0),
  );
}

function scaleResolution(resolution, factor, clampToValidTextureSize = false) {
  let width = trunc(resolution.x * factor.x);
  let height = trunc(resolution.y * factor.y);
  if (clampToValidTextureSize) {
    width = width <= 0 ? 1 : Math.min(width, 16384);
    height = height <= 0 ? 1 : Math.min(height, 16384);
  }
  return int2(width, height);
}

function scaleSize(inputSize, stretch = int2(1, 1), scale = 1) {
  return int2(trunc(inputSize.x * scale * stretch.x), trunc(inputSize.y * scale * stretch.y));
}

test("Batch 9 Int2 semantics match TiXL pure value laws", () => {
  assert.deepEqual(addInt2(int2(320, 180), int2(64, 36)), int2(384, 216));
  assert.deepEqual(makeResolution(640, 360), int2(640, 360));
  assert.deepEqual(maxInt2([int2(640, 360), int2(960, 300), int2(320, 540)]), int2(960, 540));
});

test("Int2Components exposes width, height, length, and raw float aspect ratio", () => {
  assert.deepEqual(int2Components(int2(640, 360)), {
    width: 640,
    height: 360,
    length: 230400,
    aspectRatio: 640 / 360,
  });
  assert.equal(int2Components(int2(3, 0)).aspectRatio, Infinity);
});

test("ScaleResolution and ScaleSize use TiXL C# int truncation and clamp rules", () => {
  assert.deepEqual(scaleResolution(int2(641, 361), { x: 0.5, y: 0.5 }, false), int2(320, 180));
  assert.deepEqual(scaleResolution(int2(-10, 50000), { x: 1, y: 1 }, true), int2(1, 16384));
  assert.deepEqual(scaleSize(int2(641, 361), { x: 1, y: 0.5 }, 2), int2(1282, 361));
  assert.deepEqual(scaleSize(int2(-7, 7), { x: 0.5, y: -0.5 }, 1), int2(-3, -3));
});
