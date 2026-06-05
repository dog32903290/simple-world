#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function channelInt(value) {
  return Math.trunc(clamp(value * 255, 0, 255));
}

function colorListToInts(colorLists, outputMode = 0) {
  const output = [];
  if (!colorLists) return output;

  for (const list of colorLists) {
    if (!list || list.length === 0) continue;
    for (const c of list) {
      if (outputMode === 0) output.push(channelInt(c.r), channelInt(c.g), channelInt(c.b), channelInt(c.a));
      else if (outputMode === 1) output.push(channelInt(c.a), channelInt(c.r), channelInt(c.g), channelInt(c.b));
      else if (outputMode === 2) output.push(channelInt(c.r), channelInt(c.g), channelInt(c.b));
      else if (outputMode === 3) output.push(channelInt(c.r));
      else if (outputMode === 4) output.push(channelInt(c.a));
    }
  }
  return output;
}

test("TiXL ColorListToInts emits RGBA bytes by default and truncates after clamp", () => {
  const colors = [[{ r: 1, g: 0.5, b: 0, a: 0.25 }, { r: -1, g: 2, b: 0.999, a: 1 }]];
  assert.deepEqual(colorListToInts(colors, 0), [255, 127, 0, 63, 0, 255, 254, 255]);
});

test("TiXL ColorListToInts supports ARGB, RGB, R, and A output modes", () => {
  const colors = [[{ r: 0.1, g: 0.2, b: 0.3, a: 0.4 }]];
  assert.deepEqual(colorListToInts(colors, 1), [102, 25, 51, 76]);
  assert.deepEqual(colorListToInts(colors, 2), [25, 51, 76]);
  assert.deepEqual(colorListToInts(colors, 3), [25]);
  assert.deepEqual(colorListToInts(colors, 4), [102]);
});

test("TiXL ColorListToInts skips null and empty input color lists", () => {
  const colors = [null, [], [{ r: 0, g: 1, b: 0, a: 1 }]];
  assert.deepEqual(colorListToInts(colors, 2), [0, 255, 0]);
  assert.deepEqual(colorListToInts([], 0), []);
});
