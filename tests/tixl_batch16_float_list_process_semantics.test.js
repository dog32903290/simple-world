#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function analyzeFloatList(values) {
  if (!values || values.length === 0) {
    return { min: Number.NaN, max: Number.NaN, averageMean: Number.NaN, allValid: false };
  }

  let sum = 0;
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  let allValid = true;

  for (const value of values) {
    if (!Number.isFinite(value)) {
      allValid = false;
      continue;
    }
    min = Math.min(min, value);
    max = Math.max(max, value);
    sum += value;
  }

  return { min, max, averageMean: sum / values.length, allValid };
}

function sumRange(values, lowerLimit, upperLimit, previousSelected = 0) {
  if (!values || values.length === 0) return previousSelected;

  const lower = Math.max(0, lowerLimit);
  const upper = Math.min(values.length, upperLimit);
  let sum = 0;
  for (let index = lower; index < upper; index += 1) {
    sum += values[index];
  }
  return sum;
}

function compareFloatLists(listA, listB, threshold) {
  if (!listA || listA.length === 0 || !listB || listB.length === 0) return 1;

  const maxCount = Math.max(listA.length, listB.length);
  let differentElementCount = 0;
  for (let index = 0; index < maxCount; index += 1) {
    if (index >= listA.length || index >= listB.length) {
      differentElementCount += 1;
      continue;
    }
    if (Math.abs(listA[index] - listB[index]) > threshold) {
      differentElementCount += 1;
    }
  }
  return differentElementCount / maxCount;
}

test("TiXL AnalyzeFloatList reports min, max, full-count mean, and validity", () => {
  assert.deepEqual(analyzeFloatList([5, 17]), { min: 5, max: 17, averageMean: 11, allValid: true });

  const empty = analyzeFloatList([]);
  assert.ok(Number.isNaN(empty.min));
  assert.ok(Number.isNaN(empty.max));
  assert.ok(Number.isNaN(empty.averageMean));
  assert.equal(empty.allValid, false);

  assert.deepEqual(analyzeFloatList([1, Number.NaN, 3, Number.POSITIVE_INFINITY]), {
    min: 1,
    max: 3,
    averageMean: 1,
    allValid: false,
  });

  assert.deepEqual(analyzeFloatList([Number.NaN, Number.POSITIVE_INFINITY]), {
    min: Number.POSITIVE_INFINITY,
    max: Number.NEGATIVE_INFINITY,
    averageMean: 0,
    allValid: false,
  });
});

test("TiXL SumRange uses clamped half-open index bounds and keeps previous output for empty input", () => {
  assert.equal(sumRange([5, 17, 2], 0, 999999, 7), 24);
  assert.equal(sumRange([5, 17, 2], 1, 2, 7), 17);
  assert.equal(sumRange([5, 17, 2], -10, 2, 7), 22);
  assert.equal(sumRange([5, 17, 2], 2, 1, 7), 0);
  assert.equal(sumRange([], 0, 1, 7), 7);
});

test("TiXL CompareFloatLists counts values whose absolute delta exceeds threshold", () => {
  assert.equal(compareFloatLists([], [1], 0), 1);
  assert.equal(compareFloatLists([1, 2, 3], [1, 2.2, 1], 0.5), 1 / 3);
  assert.equal(compareFloatLists([1, 2, 3], [1, 3, 1], 1), 1 / 3);
  assert.equal(compareFloatLists([1, 2, 3], [1, 2], 0.1), 1 / 3);
});
