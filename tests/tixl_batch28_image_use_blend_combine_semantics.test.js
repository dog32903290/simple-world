#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function positiveModulo(value, divisor) {
  return ((value % divisor) + divisor) % divisor;
}

function mixColor(a, b, t) {
  return a.map((channel, index) => channel + (b[index] - channel) * t);
}

function blendImages(inputs, blendFraction) {
  if (inputs.length === 0) return null;
  if (inputs.length === 1) return inputs[0];

  const f = clamp(blendFraction, 0, 10000);
  const lower = Math.trunc(f);
  const t = f - lower;
  const a = inputs[positiveModulo(lower, inputs.length)];
  const b = inputs[positiveModulo(lower + 1, inputs.length)];
  return mixColor(a, b, t);
}

function blendWithMask(imageA, colorA, imageB, colorB, mask) {
  const tintedA = imageA.map((channel, index) => channel * colorA[index]);
  const tintedB = imageB.map((channel, index) => channel * colorB[index]);
  return mixColor(tintedA, tintedB, mask[0]);
}

function selectedChannel(a, b, c, select) {
  const source = select < 5 ? a : select < 10 ? b : c;
  const mode = select % 5;
  if (mode < 3) return source[mode];
  if (mode === 3) return (source[0] + source[1] + source[2]) / 3;
  return clamp(0.239 * source[0] + 0.686 * source[1] + 0.075 * source[2], 0, 1);
}

function combine3Images(a, colorA, b, colorB, c, colorC, selectR, selectG, selectB, alphaMode) {
  const ta = a.map((channel, index) => channel * colorA[index]);
  const tb = b.map((channel, index) => channel * colorB[index]);
  const tc = c.map((channel, index) => channel * colorC[index]);
  const alpha = alphaMode === 0 ? ta[3] : alphaMode === 1 ? tb[3] : alphaMode === 2 ? tc[3] : alphaMode === 3 ? 0 : 1;
  return [
    selectedChannel(ta, tb, tc, selectR),
    selectedChannel(ta, tb, tc, selectG),
    selectedChannel(ta, tb, tc, selectB),
    alpha,
  ];
}

test("BlendImages crossfades between positive-modulo neighboring images using clamped float index", () => {
  const red = [1, 0, 0, 1];
  const green = [0, 1, 0, 1];
  const blue = [0, 0, 1, 1];

  assert.deepEqual(blendImages([red, green, blue], 1.5), [0, 0.5, 0.5, 1]);
  assert.deepEqual(blendImages([red, green, blue], 2.25), [0.25, 0, 0.75, 1]);
  assert.deepEqual(blendImages([red, green, blue], -1), red);
  assert.equal(blendImages([], 0.5), null);
});

test("BlendWithMask uses mask red channel to interpolate tinted ImageA and ImageB", () => {
  const red = [1, 0, 0, 1];
  const green = [0, 1, 0, 1];
  const white = [1, 1, 1, 1];
  const halfMask = [0.5, 0.5, 0.5, 1];

  assert.deepEqual(blendWithMask(red, white, green, white, halfMask), [0.5, 0.5, 0, 1]);
});

test("Combine3Images selects channels from tinted A/B/C and supports alpha modes", () => {
  const red = [1, 0, 0, 1];
  const green = [0, 1, 0, 0.75];
  const blue = [0, 0, 1, 0.5];
  const white = [1, 1, 1, 1];

  assert.deepEqual(combine3Images(red, white, green, white, blue, white, 0, 6, 12, 4), [1, 1, 1, 1]);
  assert.deepEqual(combine3Images(red, white, green, white, blue, white, 4, 9, 14, 2), [0.239, 0.686, 0.075, 0.5]);
});
