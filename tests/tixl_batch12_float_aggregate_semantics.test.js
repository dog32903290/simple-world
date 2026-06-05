#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function fmod(value, mod) {
  return value - mod * Math.floor(value / mod);
}

function sum(values, defaultValue = 0) {
  return values.length === 0 ? defaultValue : values.reduce((total, value) => total + value, 0);
}

function blendValues(values, f) {
  if (values.length === 0) return 0;
  const index1 = Math.trunc(fmod(Math.trunc(f), values.length));
  const index2 = Math.trunc(fmod(Math.trunc(f + 1), values.length));
  const mix = fmod(f, 1);
  return values[index1] * (1 - mix) + values[index2] * mix;
}

function remapValues(pairs, inputValue) {
  let minDistance = Infinity;
  let bestValue = 0;
  let bestIndex = -1;
  for (let index = 0; index < pairs.length; index++) {
    const pair = pairs[index];
    const distance = Math.abs(pair.x - inputValue);
    if (distance < minDistance) {
      minDistance = distance;
      bestValue = pair.y;
      bestIndex = index;
    }
  }
  return bestIndex === -1 ? 0 : bestValue;
}

test("TiXL Sum preserves empty-input default and connected accumulation", () => {
  assert.equal(sum([], 3.5), 3.5);
  assert.equal(sum([1.25, -2.5, 4], 0), 2.75);
});

test("TiXL BlendValues wraps indices with MathUtils.Fmod and blends adjacent values", () => {
  assert.equal(blendValues([], 0.5), 0);
  assert.equal(blendValues([10, 20, 40], 0.5), 15);
  assert.equal(blendValues([10, 20, 40], 2.25), 32.5);
  assert.equal(blendValues([10, 20, 40], -0.25), 10);
});

test("TiXL RemapValues picks y from the closest x pair and falls back to zero", () => {
  const pairs = [{ x: 0, y: 10 }, { x: 5, y: 50 }, { x: 9, y: 90 }];
  assert.equal(remapValues([], 4), 0);
  assert.equal(remapValues(pairs, 4.6), 50);
  assert.equal(remapValues(pairs, 8.2), 90);
  assert.equal(remapValues([{ x: 3, y: 30 }, { x: 5, y: 50 }], 4), 30);
});
