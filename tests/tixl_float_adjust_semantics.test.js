#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function csharpRemainder(value, divisor) {
  return value - Math.trunc(value / divisor) * divisor;
}

function tixlAbs(value) {
  return value > 0 ? value : -1 * value;
}

function tixlCeil(value) {
  return Math.ceil(value);
}

function tixlFloor(value) {
  return Math.trunc(value);
}

function tixlRoundValue2(i, stepsPerUnit, stepRatio) {
  const u = 1 / stepsPerUnit;
  const v = stepRatio / (2 * stepsPerUnit);
  const m = csharpRemainder(i, u);
  const r = m - (m < v
    ? 0
    : m > u - v
      ? u
      : (m - v) / (1 - 2 * stepsPerUnit * v));
  const y = i - r;
  return y;
}

function tixlInvertFloat(a, invert) {
  const sign = invert ? -1 : 1;
  return sign * a;
}

function tixlClamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

const cases = [
  ["Abs positive passes through", tixlAbs(3.25), 3.25],
  ["Abs negative flips sign", tixlAbs(-3.25), 3.25],
  ["Ceil positive fraction", tixlCeil(2.1), 3],
  ["Ceil negative fraction", tixlCeil(-2.9), -2],
  ["Floor mirrors TiXL int cast for positive values", tixlFloor(2.9), 2],
  ["Floor mirrors TiXL int cast for negative values", tixlFloor(-2.9), -2],
  ["Round default ratio preserves fractional value", tixlRoundValue2(1.25, 1, 0), 1.25],
  ["Round preserves exact whole step", tixlRoundValue2(2, 1, 0), 2],
  ["Round default ratio preserves half-step fractional value", tixlRoundValue2(1.3, 2, 0), 1.3],
  ["Round supports negative C# remainder behavior", tixlRoundValue2(-1.3, 2, 0), -1],
  ["RoundRatio snaps near upper edge", tixlRoundValue2(0.75, 1, 0.5), 1],
  ["RoundRatio blends middle interval", tixlRoundValue2(0.75, 1, 0.25), 0.8333333333333334],
  ["InvertFloat default inverts", tixlInvertFloat(4, true), -4],
  ["InvertFloat can pass through", tixlInvertFloat(4, false), 4],
  ["Clamp limits high value", tixlClamp(12, 0, 10), 10],
  ["Clamp limits low value", tixlClamp(-2, 0, 10), 0],
  ["Clamp does not sort reversed min and max", tixlClamp(5, 10, 0), 0],
];

for (const [name, actual, expected] of cases) {
  test(name, () => {
    assert.ok(Math.abs(actual - expected) < 1e-12, `${actual} !== ${expected}`);
  });
}
