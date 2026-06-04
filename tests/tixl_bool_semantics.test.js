const assert = require("node:assert/strict");
const test = require("node:test");

test("And matches TiXL bool combine semantics", () => {
  assert.equal(and(false, false), false);
  assert.equal(and(false, true), false);
  assert.equal(and(true, false), false);
  assert.equal(and(true, true), true);
});

test("Or matches TiXL bool combine semantics", () => {
  assert.equal(or(false, false), false);
  assert.equal(or(false, true), true);
  assert.equal(or(true, false), true);
  assert.equal(or(true, true), true);
});

test("Not matches TiXL bool inversion semantics", () => {
  assert.equal(not(false), true);
  assert.equal(not(true), false);
});

test("Xor matches TiXL B-gated inversion semantics", () => {
  assert.equal(xor(false, false), false);
  assert.equal(xor(false, true), true);
  assert.equal(xor(true, false), true);
  assert.equal(xor(true, true), false);
});

test("BoolToFloat and BoolToInt choose the TiXL true/false result values", () => {
  assert.equal(boolToFloat(false, 0.25, 4.5), 0.25);
  assert.equal(boolToFloat(true, 0.25, 4.5), 4.5);
  assert.equal(boolToInt(false, -3, 8), -3);
  assert.equal(boolToInt(true, -3, 8), 8);
});

test("PickBool two-input Vuo adapter preserves TiXL modulo index selection for two bools", () => {
  assert.equal(pickBool([false, true], 0), false);
  assert.equal(pickBool([false, true], 1), true);
  assert.equal(pickBool([false, true], 2), false);
  assert.equal(pickBool([false, true], -1), true);
});

function and(a, b) {
  return a & b ? true : false;
}

function or(a, b) {
  return a || b;
}

function not(boolValue) {
  return !boolValue;
}

function xor(a, b) {
  return b ? !a : a;
}

function boolToFloat(boolValue, forFalse, forTrue) {
  return boolValue ? forTrue : forFalse;
}

function boolToInt(boolValue, resultForFalse, resultForTrue) {
  return boolValue ? resultForTrue : resultForFalse;
}

function pickBool(boolValues, index) {
  return boolValues[mod(index, boolValues.length)];
}

function mod(value, count) {
  return ((value % count) + count) % count;
}
