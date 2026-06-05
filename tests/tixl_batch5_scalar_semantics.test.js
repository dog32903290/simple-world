#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function tixlSigmoid(value, stretch) {
  return 1 / (1 + Math.pow(Math.E, stretch * value));
}

function tixlLog(value, base) {
  if (value === 1 && base !== 1) {
    return 0;
  }
  if (base === 1 || value < 0 || base <= 0) {
    return Number.NaN;
  }
  return Math.log(value) / Math.log(base);
}

function clampInt(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function tixlCompare(value, testValue, mode, precision) {
  switch (clampInt(mode, 0, 3)) {
    case 0:
      return value < testValue;
    case 1:
      return Math.abs(value - testValue) < precision;
    case 2:
      return value > testValue;
    case 3:
      return Math.abs(value - testValue) >= precision;
    default:
      throw new Error("unreachable");
  }
}

test("TiXL Sigmoid scalar reference cases", () => {
  assert.equal(tixlSigmoid(0, 1), 0.5);
  assert.ok(tixlSigmoid(1, 1) < 0.5, "positive input lowers result in TiXL source");
  assert.ok(tixlSigmoid(-1, 1) > 0.5, "negative input raises result in TiXL source");
  assert.equal(tixlSigmoid(2, 0), 0.5);
});

test("TiXL Log scalar reference cases", () => {
  assert.equal(tixlLog(8, 2), 3);
  assert.equal(tixlLog(1, 10), 0);
  assert.equal(tixlLog(1, 0), 0, "C# Math.Log returns 0 when value is 1 and base is 0");
  assert.ok(Number.isNaN(tixlLog(8, 1)), "TiXL default base 1 yields NaN through Math.Log");
  assert.ok(Number.isNaN(tixlLog(8, 0)), "base 0 with non-1 value yields NaN");
  assert.ok(Number.isNaN(tixlLog(-1, 2)), "negative value yields NaN");
});

test("TiXL Compare scalar reference cases", () => {
  assert.equal(tixlCompare(1, 2, 0, 0.001), true);
  assert.equal(tixlCompare(2, 2.0005, 1, 0.001), true);
  assert.equal(tixlCompare(3, 2, 2, 0.001), true);
  assert.equal(tixlCompare(2, 2.1, 3, 0.001), true);
  assert.equal(tixlCompare(2, 2.0005, 3, 0.001), false);
  assert.equal(tixlCompare(3, 2, 99, 0.001), true, "mode clamps to IsNotEqual");
  assert.equal(tixlCompare(1, 2, -3, 0.001), true, "mode clamps to IsSmaller");
});
