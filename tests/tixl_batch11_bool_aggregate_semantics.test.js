#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function all(values) {
  let result = true;
  let anyConnected = false;
  for (const value of values) {
    anyConnected = true;
    result = result && value;
  }
  return result && anyConnected;
}

function any(values) {
  let result = false;
  for (const value of values) result = result || value;
  return result;
}

test("TiXL All returns false for empty input and true only when every connected value is true", () => {
  assert.equal(all([]), false);
  assert.equal(all([true]), true);
  assert.equal(all([true, true, true]), true);
  assert.equal(all([true, false, true]), false);
});

test("TiXL Any returns false for empty input and true when at least one connected value is true", () => {
  assert.equal(any([]), false);
  assert.equal(any([false, false]), false);
  assert.equal(any([false, true, false]), true);
});
