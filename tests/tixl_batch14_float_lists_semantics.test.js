#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function positiveMod(value, mod) {
  if (mod === 0) return 0;
  const result = value % mod;
  return result < 0 ? result + mod : result;
}

function floatsToList(values) {
  return [...values];
}

function floatListLength(list) {
  return list ? list.length : 0;
}

function pickFloatFromList(list, index) {
  if (!list || list.length === 0) return 0;
  return list[positiveMod(index, list.length)];
}

function setFloatListValue(list, index, value, mode, triggerSet, previous = []) {
  if (!triggerSet || !list || list.length === 0) return [...previous];
  const result = [...list];
  const apply = (position) => {
    if (mode === 1) result[position] += value;
    else if (mode === 2) result[position] *= value;
    else result[position] = value;
  };
  if (index >= 0) apply(positiveMod(index, result.length));
  else if (index === -2) result.forEach((_, position) => apply(position));
  return result;
}

test("TiXL FloatsToList preserves connected input order", () => {
  assert.deepEqual(floatsToList([1.25, -2.5, 4]), [1.25, -2.5, 4]);
  assert.deepEqual(floatsToList([]), []);
});

test("TiXL FloatListLength returns zero for null and empty lists", () => {
  assert.equal(floatListLength(null), 0);
  assert.equal(floatListLength([]), 0);
  assert.equal(floatListLength([3.5, 4.5]), 2);
});

test("TiXL PickFloatFromList uses positive modulo and zero for empty lists", () => {
  assert.equal(pickFloatFromList([], 1), 0);
  assert.equal(pickFloatFromList([10, 20, 30], -1), 30);
  assert.equal(pickFloatFromList([10, 20, 30], 4), 20);
});

test("TiXL SetFloatListValue preserves trigger, wrapped index, all-index, no-op -1, and modes", () => {
  assert.deepEqual(setFloatListValue([1, 2, 3], 4, 10, 0, true), [1, 10, 3]);
  assert.deepEqual(setFloatListValue([1, 2, 3], -2, 10, 1, true), [11, 12, 13]);
  assert.deepEqual(setFloatListValue([1, 2, 3], -2, 10, 2, true), [10, 20, 30]);
  assert.deepEqual(setFloatListValue([1, 2, 3], -1, 10, 0, true), [1, 2, 3]);
  assert.deepEqual(setFloatListValue([1, 2, 3], 1, 10, 0, false, [7, 8]), [7, 8]);
});
