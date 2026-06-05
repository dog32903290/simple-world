#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function tixlSin(input, period, phase, amplitude, offset) {
  return Math.sin(input / period + phase) * amplitude + offset;
}

function tixlCos(input) {
  return Math.cos(input);
}

function assertNear(actual, expected, name) {
  assert.ok(Math.abs(actual - expected) < 1e-12, `${name}: ${actual} !== ${expected}`);
}

test("TiXL Sin uses radians, period, phase, amplitude, and offset", () => {
  assertNear(tixlSin(0, 1, 0, 1, 0), 0, "Sin default");
  assertNear(tixlSin(Math.PI / 2, 1, 0, 1, 0), 1, "Sin radians");
  assertNear(tixlSin(Math.PI, 2, 0, 1, 0), 1, "Sin period divides input");
  assertNear(tixlSin(0, 1, Math.PI / 2, 2, 3), 5, "Sin phase amplitude offset");
});

test("TiXL Cos uses radians", () => {
  assertNear(tixlCos(0), 1, "Cos default");
  assertNear(tixlCos(Math.PI), -1, "Cos pi");
  assertNear(tixlCos(Math.PI / 2), 0, "Cos half pi");
});
