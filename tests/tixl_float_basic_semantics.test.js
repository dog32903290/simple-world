#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function tixlAdd(Input1, Input2) {
  return Input1 + Input2;
}

function tixlSub(Input1, Input2) {
  return Input1 - Input2;
}

function tixlMultiply(A, B) {
  return A * B;
}

function tixlDiv(A, B) {
  return B === 0 ? Number.NaN : A / B;
}

function tixlModulo(Value, ModuloValue) {
  if (ModuloValue !== 0) {
    return Value - ModuloValue * Math.floor(Value / ModuloValue);
  }

  return 0;
}

function tixlSqrt(Value) {
  return Math.sqrt(Value);
}

function tixlPow(Value, Exponent) {
  return Math.pow(Value, Exponent);
}

function assertNumber(actual, expected, name) {
  if (Number.isNaN(expected)) {
    assert.ok(Number.isNaN(actual), name);
    return;
  }

  assert.equal(actual, expected, name);
}

const cases = [
  ["Add default", tixlAdd(0, 0), 0],
  ["Add signed values", tixlAdd(-2.5, 10), 7.5],
  ["Sub default", tixlSub(0, 0), 0],
  ["Sub preserves port order", tixlSub(2, 10), -8],
  ["Multiply default", tixlMultiply(1, 1), 1],
  ["Multiply signed values", tixlMultiply(-3, 4), -12],
  ["Div default", tixlDiv(1, 1), 1],
  ["Div normal quotient", tixlDiv(7, 2), 3.5],
  ["Div zero denominator returns NaN", tixlDiv(1, 0), Number.NaN],
  ["Modulo default", tixlModulo(0, 1), 0],
  ["Modulo positive values", tixlModulo(7, 3), 1],
  ["Modulo negative dividend uses floor semantics", tixlModulo(-1, 3), 2],
  ["Modulo zero divisor returns 0", tixlModulo(5, 0), 0],
  ["Sqrt default", tixlSqrt(1), 1],
  ["Sqrt positive value", tixlSqrt(9), 3],
  ["Sqrt negative value returns NaN", tixlSqrt(-1), Number.NaN],
  ["Pow default", tixlPow(1, 1), 1],
  ["Pow fractional exponent", tixlPow(4, 0.5), 2],
  ["Pow zero exponent", tixlPow(5, 0), 1],
];

test("TiXL Lib.numbers.float.basic scalar reference cases", () => {
  for (const [name, actual, expected] of cases) {
    assertNumber(actual, expected, name);
  }
});
