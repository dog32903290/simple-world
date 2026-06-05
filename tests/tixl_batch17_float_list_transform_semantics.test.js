#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function combineFloatLists(lists) {
  const output = [];
  if (!lists || lists.length === 0) return output;
  for (const list of lists) {
    if (list && list.length > 0) output.push(...list);
  }
  return output;
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function bias(b, x) {
  return x / (((1 / b - 2) * (1 - x)) + 1);
}

function schlickBias(g, x) {
  if (x < 0.5) {
    x *= 2;
    return 0.5 * bias(g, x);
  }
  x = 2 * x - 1;
  return 0.5 * bias(1 - g, x) + 0.5;
}

function applyGainAndBias(value, gain, biasValue) {
  const b = clamp(biasValue, 0, 1);
  const g = clamp(gain, 0, 1);
  if (value > 0.999) return 1;
  if (value < 0.00001) return 0;
  if (g < 0.5) return schlickBias(g, bias(b, value));
  return bias(b, schlickBias(g, value));
}

function positiveFmod(value, mod) {
  return value - mod * Math.floor(value / mod);
}

function remapFloatList(values, inMin, inMax, outMin, outMax, biasAndGain = { x: 0.5, y: 0.5 }, mode = 0) {
  if (!values || values.length === 0) return [];
  const inRange = inMax - inMin;
  if (Math.abs(inRange) < 0.00001) return values.map(() => outMin);

  return values.map((value) => {
    let normalized = (value - inMin) / inRange;
    if (normalized > 0 && normalized < 1) {
      normalized = applyGainAndBias(normalized, biasAndGain.x, biasAndGain.y);
    }
    let result = normalized * (outMax - outMin) + outMin;
    if (mode === 1) {
      result = clamp(result, Math.min(outMin, outMax), Math.max(outMin, outMax));
    } else if (mode === 2) {
      const min = Math.min(outMin, outMax);
      const max = Math.max(outMin, outMax);
      const range = max - min;
      result = Math.abs(range) > 0.00001 ? min + positiveFmod(result - min, range) : min;
    }
    return result;
  });
}

test("TiXL CombineFloatLists concatenates connected non-empty lists in input order", () => {
  assert.deepEqual(combineFloatLists([[1, 2], [], [10], null, [3, 4]]), [1, 2, 10, 3, 4]);
  assert.deepEqual(combineFloatLists([]), []);
});

test("TiXL RemapFloatList maps each value and returns empty for empty input", () => {
  assert.deepEqual(remapFloatList([], 0, 1, 0, 10), []);
  assert.deepEqual(remapFloatList([0, 0.5, 1], 0, 1, 10, 20), [10, 15, 20]);
  assert.deepEqual(remapFloatList([5, 17], 0, 0, 2, 9), [2, 2]);
});

test("TiXL RemapFloatList supports clamped and modulo output modes", () => {
  assert.deepEqual(remapFloatList([-1, 0.5, 2], 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 1), [10, 15, 20]);
  assert.deepEqual(remapFloatList([-0.25, 1.25], 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 2), [17.5, 12.5]);
});
