#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function mod(value, repeat) {
  if (repeat === 0) return 0;
  const x = value % repeat;
  return x < 0 ? repeat + x : x;
}

function compareInt(value, testValue, mode, resultForTrue, resultForFalse) {
  const modeClamped = clamp(mode, 0, 3);
  const isTrue = [
    value < testValue,
    value === testValue,
    value > testValue,
    value !== testValue,
  ][modeClamped];
  return { isTrue, resultValue: isTrue ? resultForTrue : resultForFalse };
}

function computePrime(index) {
  if (index < 1) return -1;
  let count = 0;
  let n = 2;
  while (true) {
    if (count > 10000) return -1;
    let isPrime = true;
    const limit = Math.trunc(Math.sqrt(n));
    for (let i = 2; i <= limit; ++i) {
      if (n % i === 0) {
        isPrime = false;
        break;
      }
    }
    if (isPrime && ++count === index) return n;
    n = n === 2 ? 3 : n + 2;
  }
}

test("TiXL CompareInt clamps modes and emits bool plus selected integer", () => {
  assert.deepEqual(compareInt(2, 4, 0, 10, -10), { isTrue: true, resultValue: 10 });
  assert.deepEqual(compareInt(4, 4, 1, 10, -10), { isTrue: true, resultValue: 10 });
  assert.deepEqual(compareInt(5, 4, 2, 10, -10), { isTrue: true, resultValue: 10 });
  assert.deepEqual(compareInt(5, 5, 3, 10, -10), { isTrue: false, resultValue: -10 });
  assert.deepEqual(compareInt(1, 2, -4, 1, 0), { isTrue: true, resultValue: 1 });
  assert.deepEqual(compareInt(1, 2, 99, 1, 0), { isTrue: true, resultValue: 1 });
});

test("TiXL PickInt wraps indexes with MathUtils.Mod", () => {
  const values = [11, 22, 33];
  assert.equal(values[mod(4, values.length)], 22);
  assert.equal(values[mod(-1, values.length)], 33);
});

test("TiXL int process scalar reference cases", () => {
  assert.equal(clamp(12, 0, 10), 10);
  assert.equal(clamp(-2, 0, 10), 0);
  assert.equal(clamp(5, 10, 0), 0);
  assert.equal(Math.trunc(3.9), 3);
  assert.equal(Math.trunc(-3.9), -3);
  assert.equal(computePrime(0), -1);
  assert.equal(computePrime(1), 2);
  assert.equal(computePrime(6), 13);
  assert.equal(Math.max(...[4, -2, 9]), 9);
  assert.equal(Math.min(...[4, -2, 9]), -2);
});
