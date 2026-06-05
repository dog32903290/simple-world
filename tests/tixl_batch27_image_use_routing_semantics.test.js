#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function firstValidTexture(inputs, previousOutput) {
  for (const input of inputs) {
    if (input !== null && input !== undefined) {
      return input;
    }
  }
  return previousOutput;
}

function positiveModulo(value, divisor) {
  return ((value % divisor) + divisor) % divisor;
}

function pickTexture(inputs, index, previousSelected) {
  if (!inputs.length) {
    return previousSelected;
  }
  return inputs[positiveModulo(index, inputs.length)];
}

function swapTextures(textureAInput, textureBInput, enableSwap) {
  return enableSwap
    ? { textureA: textureBInput, textureB: textureAInput }
    : { textureA: textureAInput, textureB: textureBInput };
}

function useFallbackTexture(textureA, fallback) {
  return textureA ?? fallback;
}

test("FirstValidTexture returns the first non-null input and preserves previous output when all are invalid", () => {
  const a = { id: "a" };
  const b = { id: "b" };
  const previous = { id: "previous" };

  assert.equal(firstValidTexture([null, a, b], previous), a);
  assert.equal(firstValidTexture([undefined, null], previous), previous);
});

test("PickTexture uses TiXL positive modulo and preserves previous output when no inputs are connected", () => {
  const a = { id: "a" };
  const b = { id: "b" };
  const c = { id: "c" };
  const previous = { id: "previous" };

  assert.equal(pickTexture([a, b, c], 0, previous), a);
  assert.equal(pickTexture([a, b, c], -1, previous), c);
  assert.equal(pickTexture([a, b, c], 4, previous), b);
  assert.equal(pickTexture([], 4, previous), previous);
});

test("SwapTextures passes through by default and swaps only when EnableSwap is true", () => {
  const a = { id: "a" };
  const b = { id: "b" };

  assert.deepEqual(swapTextures(a, b, false), { textureA: a, textureB: b });
  assert.deepEqual(swapTextures(a, b, true), { textureA: b, textureB: a });
});

test("UseFallbackTexture returns TextureA when present and Fallback when TextureA is null", () => {
  const a = { id: "a" };
  const fallback = { id: "fallback" };

  assert.equal(useFallbackTexture(a, fallback), a);
  assert.equal(useFallbackTexture(null, fallback), fallback);
});
