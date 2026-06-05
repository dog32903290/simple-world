#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a = 1) {
  return { r, g, b, a };
}

function pickPixel(image, position, previous = color(0, 0, 0, 1)) {
  if (!image) return previous;
  const column = Math.min(Math.max(Math.trunc(position.x * image.width), 0), image.width - 1);
  const row = Math.min(Math.max(Math.trunc(position.y * image.height), 0), image.height - 1);
  return image.pixels[row][column];
}

test("TiXL PickColorFromImage converts normalized position to clamped integer pixel coordinates", () => {
  const image = {
    width: 3,
    height: 2,
    pixels: [
      [color(1, 0, 0), color(0, 1, 0), color(0, 0, 1)],
      [color(1, 1, 0), color(0, 1, 1), color(1, 0, 1)],
    ],
  };

  assert.deepEqual(pickPixel(image, { x: 0, y: 0 }), color(1, 0, 0));
  assert.deepEqual(pickPixel(image, { x: 0.67, y: 0.75 }), color(1, 0, 1));
  assert.deepEqual(pickPixel(image, { x: -10, y: 10 }), color(1, 1, 0));
  assert.deepEqual(pickPixel(image, { x: 1, y: 1 }), color(1, 0, 1));
});

test("TiXL PickColorFromImage preserves previous output when image is null", () => {
  const previous = color(0.25, 0.5, 0.75, 1);
  assert.deepEqual(pickPixel(null, { x: 0.5, y: 0.5 }, previous), previous);
});
