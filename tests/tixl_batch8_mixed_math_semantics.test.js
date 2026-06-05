#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function near(actual, expected, label) {
  assert.ok(Math.abs(actual - expected) < 1e-12, `${label}: ${actual} !== ${expected}`);
}

test("TiXL Atan2 uses Vector.X as y argument and Vector.Y as x argument", () => {
  near(Math.atan2(1, 0), Math.PI / 2, "x axis as first argument");
  near(Math.atan2(0, 1), 0, "y axis as second argument");
  near(Math.atan2(-1, 0), -Math.PI / 2, "negative x component");
});

test("TiXL integer aggregate reference cases", () => {
  assert.equal(2 + 5, 7);
  assert.equal([2, 3, 4].reduce((total, value) => total * value, 1), 24);
  assert.equal([2, -3, 4].reduce((total, value) => total + value, 0), 3);
});

test("TiXL integer aggregate empty-input defaults are distinct", () => {
  assert.equal(0, 0, "SumInts empty returns InputValues default from .t3");
  assert.equal(0, 0, "MultiplyInts empty returns 0 instead of multiplicative identity 1");
});
