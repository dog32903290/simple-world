#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a = 1) {
  return { r, g, b, a };
}

function gradient(start, end) {
  return { steps: [{ position: 0, color: start }, { position: 1, color: end }] };
}

function lerpColor(a, b, t) {
  return color(
    a.r + (b.r - a.r) * t,
    a.g + (b.g - a.g) * t,
    a.b + (b.b - a.b) * t,
    a.a + (b.a - a.a) * t,
  );
}

function sampleGradient(g, t) {
  return lerpColor(g.steps[0].color, g.steps[1].color, t);
}

function gradientsToTexture(gradients, resolution, direction) {
  const sampleCount = Math.min(Math.max(resolution, 1), 16384);
  const width = direction === 0 ? sampleCount : gradients.length;
  const height = direction === 0 ? gradients.length : sampleCount;
  const pixels = Array.from({ length: height }, () => Array.from({ length: width }, () => color(0, 0, 0, 0)));

  if (direction === 0) {
    gradients.forEach((g, row) => {
      for (let i = 0; i < sampleCount; i++) {
        pixels[row][i] = sampleGradient(g, sampleCount === 1 ? 0 : i / (sampleCount - 1));
      }
    });
  } else {
    for (let i = 0; i < sampleCount; i++) {
      gradients.forEach((g, column) => {
        pixels[i][column] = sampleGradient(g, sampleCount === 1 ? 0 : i / (sampleCount - 1));
      });
    }
  }

  return { width, height, pixels };
}

test("TiXL GradientsToTexture lays out gradients horizontally by default", () => {
  const texture = gradientsToTexture([
    gradient(color(1, 0, 0), color(0, 0, 1)),
    gradient(color(0, 1, 0), color(1, 1, 0)),
  ], 3, 0);

  assert.equal(texture.width, 3);
  assert.equal(texture.height, 2);
  assert.deepEqual(texture.pixels[0][0], color(1, 0, 0));
  assert.deepEqual(texture.pixels[0][1], color(0.5, 0, 0.5));
  assert.deepEqual(texture.pixels[1][2], color(1, 1, 0));
});

test("TiXL GradientsToTexture lays out gradients vertically when Direction is 1", () => {
  const texture = gradientsToTexture([
    gradient(color(1, 0, 0), color(0, 0, 1)),
    gradient(color(0, 1, 0), color(1, 1, 0)),
  ], 3, 1);

  assert.equal(texture.width, 2);
  assert.equal(texture.height, 3);
  assert.deepEqual(texture.pixels[0][0], color(1, 0, 0));
  assert.deepEqual(texture.pixels[1][1], color(0.5, 1, 0));
  assert.deepEqual(texture.pixels[2][0], color(0, 0, 1));
});
