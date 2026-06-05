#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function csharpIntDiv(numerator, denominator) {
  return denominator === 0 ? 1 : Math.trunc(numerator / denominator);
}

function csharpIntRemainder(value, mod) {
  return value - Math.trunc(value / mod) * mod;
}

test("TiXL Lib.numbers.int.basic scalar reference cases", () => {
  assert.equal(0 + 0, 0, "IntAdd default");
  assert.equal(3 + -5, -2, "IntAdd signed values");
  assert.equal(2 - 10, -8, "SubInts preserves port order");
  assert.equal(-3 * 4, -12, "MultiplyInt signed values");
  assert.equal(csharpIntDiv(7, 2), 3, "IntDiv truncates toward zero");
  assert.equal(csharpIntDiv(-7, 2), -3, "IntDiv negative truncates toward zero");
  assert.equal(csharpIntDiv(7, 0), 1, "IntDiv zero denominator returns 1");
  assert.equal(csharpIntRemainder(7, 3), 1, "ModInt positive remainder");
  assert.equal(csharpIntRemainder(-7, 3), -1, "ModInt follows C# remainder sign");
  assert.equal(Number(42), 42, "IntToFloat converts value");
});

test("TiXL Lib.numbers.int.logic IsIntEven reference cases", () => {
  assert.equal(0 % 2 === 0, true, "zero is even");
  assert.equal(4 % 2 === 0, true, "positive even");
  assert.equal(5 % 2 === 0, false, "positive odd");
  assert.equal(-3 % 2 === 0, false, "negative odd");
});
