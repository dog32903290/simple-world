#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function tixlSmootherStep(min, max, value) {
  const t = clamp((value - min) / (max - min), 0, 1);
  return t * t * t * (t * (t * 6 - 15) + 10);
}

const cases = [
  ["SmoothStep default reaches upper edge", tixlSmootherStep(0, 1, 1), 1],
  ["SmoothStep clamps below min", tixlSmootherStep(0, 1, -0.25), 0],
  ["SmoothStep midpoint is half", tixlSmootherStep(0, 1, 0.5), 0.5],
  ["SmoothStep clamps above max", tixlSmootherStep(0, 1, 2), 1],
  ["SmoothStep uses smootherstep, not classic smoothstep", tixlSmootherStep(0, 1, 0.25), 0.103515625],
  ["SmoothStep supports shifted ranges", tixlSmootherStep(10, 20, 15), 0.5],
];

test("TiXL Lib.numbers.float.process SmoothStep reference cases", () => {
  for (const [name, actual, expected] of cases) {
    assert.ok(Math.abs(actual - expected) < 1e-12, `${name}: ${actual} !== ${expected}`);
  }
});
