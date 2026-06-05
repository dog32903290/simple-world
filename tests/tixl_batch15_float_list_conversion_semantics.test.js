#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function positiveMod(value, mod) {
  if (mod === 0) return 0;
  const result = value % mod;
  return result < 0 ? result + mod : result;
}

function intListToFloatList(values) {
  return values ? values.map((value) => Number(value)) : [];
}

function floatListToIntList(values) {
  return values ? values.map((value) => Math.trunc(value)) : [];
}

function pickFloatList(inputLists, index, previous = []) {
  if (!inputLists || inputLists.length === 0) return previous;
  return inputLists[positiveMod(index, inputLists.length)];
}

test("TiXL IntListToFloatList returns an empty list for null and casts each int to float", () => {
  assert.deepEqual(intListToFloatList(null), []);
  assert.deepEqual(intListToFloatList([1, -2, 30]), [1, -2, 30]);
});

test("TiXL FloatListToIntList returns an empty list for null and truncates toward zero", () => {
  assert.deepEqual(floatListToIntList(null), []);
  assert.deepEqual(floatListToIntList([1.9, -2.9, 3.0]), [1, -2, 3]);
});

test("TiXL PickFloatList uses positive modulo across connected list inputs and keeps prior output when none are connected", () => {
  const lists = [[1, 2], [10, 20, 30], [99]];
  assert.deepEqual(pickFloatList(lists, -1), [99]);
  assert.deepEqual(pickFloatList(lists, 4), [10, 20, 30]);
  assert.deepEqual(pickFloatList([], 0, [7, 8]), [7, 8]);
});
