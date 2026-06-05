#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function tixlMod(value, repeat) {
  if (repeat === 0) return 0;
  const x = value % repeat;
  return x < 0 ? repeat + x : x;
}

function tixlPickFloat(values, index) {
  if (!values.length) return 0;
  return values[tixlMod(index, values.length)];
}

function tixlValueToRate(value, ratesText) {
  const ratios = ratesText
    .split("\n")
    .filter((line) => line.trim().length > 0)
    .map((line) => Number(line))
    .filter((value) => Number.isFinite(value));
  if (!ratios.length) return 1;
  const f = Math.min(Math.max(value, 0), 0.99);
  return ratios[Math.trunc((ratios.length - 1) * f + 0.5)];
}

test("TiXL IsGreater uses strict greater-than", () => {
  assert.equal(1 > 0.5, true);
  assert.equal(0.5 > 0.5, false);
});

test("TiXL PickFloat wraps negative and overflowing indexes with MathUtils.Mod", () => {
  assert.equal(tixlPickFloat([2, 4, 8], 0), 2);
  assert.equal(tixlPickFloat([2, 4, 8], 4), 4);
  assert.equal(tixlPickFloat([2, 4, 8], -1), 8);
});

test("TiXL TryParse returns parsed value or Default", () => {
  assert.equal(Number.parseFloat("3.25"), 3.25);
  assert.equal(Number.isNaN(Number("not-a-number")), true);
});

test("TiXL ValueToRate parses newline ratios and clamps value before index selection", () => {
  const rates = "0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32";
  assert.equal(tixlValueToRate(0.5, rates), 1);
  assert.equal(tixlValueToRate(1.5, rates), 32);
  assert.equal(tixlValueToRate(-1, rates), 0);
  assert.equal(tixlValueToRate(0.75, "1\nbad\n4\n8"), 8);
  assert.equal(tixlValueToRate(0.5, ""), 1);
});
