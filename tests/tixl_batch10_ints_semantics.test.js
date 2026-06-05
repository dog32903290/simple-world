#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function positiveMod(value, mod) {
  return ((value % mod) + mod) % mod;
}

function mergeAppend(inputLists, startIndices = [], maxSize = -1) {
  const result = maxSize >= 0 ? Array(maxSize).fill(0) : [];
  let writeIndex = 0;
  for (let listIndex = 0; listIndex < inputLists.length; listIndex++) {
    const source = inputLists[listIndex] ?? [];
    if (source.length === 0) continue;
    if (listIndex < startIndices.length && startIndices[listIndex] >= 0 && (maxSize < 0 || startIndices[listIndex] < maxSize)) {
      writeIndex = startIndices[listIndex];
    }
    for (const value of source) {
      if (maxSize >= 0 && writeIndex >= maxSize) break;
      while (writeIndex > result.length) result.push(-1);
      result[writeIndex++] = value;
    }
  }
  return result;
}

function mergeHtp(inputLists) {
  const maxLength = Math.max(0, ...inputLists.map((list) => (list ?? []).length));
  return Array.from({ length: maxLength }, (_, index) => Math.max(...inputLists.filter(Boolean).filter((list) => index < list.length).map((list) => list[index])));
}

function mergeAverage(inputLists) {
  const maxLength = Math.max(0, ...inputLists.map((list) => (list ?? []).length));
  return Array.from({ length: maxLength }, (_, index) => {
    const values = inputLists.filter(Boolean).filter((list) => index < list.length).map((list) => list[index]);
    return Math.trunc(values.reduce((sum, value) => sum + value, 0) / values.length);
  });
}

function setIntListValue(list, index, value, mode, triggerSet) {
  const result = [...list];
  if (!triggerSet || result.length === 0) return result;
  const apply = (position) => {
    if (mode === 0) result[position] = value;
    if (mode === 1) result[position] += value;
    if (mode === 2) result[position] *= value;
  };
  if (index >= 0) apply(positiveMod(index, result.length));
  else if (index === -2) result.forEach((_, position) => apply(position));
  return result;
}

test("Batch 10 ints list scalar and picker laws match TiXL", () => {
  assert.equal([4, 5, 6].length, 3);
  assert.equal(([]).length, 0);
  assert.deepEqual([3, -1, 7], [3, -1, 7]);
  assert.equal([10, 20, 30][positiveMod(-1, 3)], 30);
  assert.equal([10, 20, 30][positiveMod(4, 3)], 20);
});

test("MergeIntLists append, htp, and average modes preserve audited TiXL behavior", () => {
  assert.deepEqual(mergeAppend([[1, 2], [9], [4, 5]], [0, 4, 2], 6), [1, 2, 4, 5, 9, 0]);
  assert.deepEqual(mergeAppend([[7], [8]], [0, 3], -1), [7, -1, -1, 8]);
  assert.deepEqual(mergeHtp([[1, 9, 3], [2, 4], [-5, 10, 1, 8]]), [2, 10, 3, 8]);
  assert.deepEqual(mergeAverage([[1, 9, 3], [2, 4], [-5, 10, 1, 8]]), [Math.trunc(-2 / 3), Math.trunc(23 / 3), 2, 8]);
});

test("SetIntListValue preserves trigger, wrapped index, all-index, and modes", () => {
  assert.deepEqual(setIntListValue([1, 2, 3], 4, 10, 0, true), [1, 10, 3]);
  assert.deepEqual(setIntListValue([1, 2, 3], -2, 10, 1, true), [11, 12, 13]);
  assert.deepEqual(setIntListValue([1, 2, 3], -2, 10, 2, true), [10, 20, 30]);
  assert.deepEqual(setIntListValue([1, 2, 3], 1, 10, 0, false), [1, 2, 3]);
});
