#!/usr/bin/env node

const assert = require("node:assert/strict");

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function tixlLerp(a, b, f, shouldClamp) {
  if (shouldClamp) {
    f = clamp(f, 0, 1);
  }
  return a + (b - a) * f;
}

const cases = [
  ["default returns A", tixlLerp(0, 1, 0, false), 0],
  ["interpolates inside range", tixlLerp(10, 20, 0.25, false), 12.5],
  ["extrapolates when clamp is false", tixlLerp(10, 20, 1.5, false), 25],
  ["clamps factor when clamp is true", tixlLerp(10, 20, 1.5, true), 20],
  ["clamps negative factor when clamp is true", tixlLerp(10, 20, -0.5, true), 10],
];

for (const [name, actual, expected] of cases) {
  assert.equal(actual, expected, name);
}

console.log(`PASS ${cases.length} TiXL Lerp semantic cases`);
