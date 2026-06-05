#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function getBias(b, x) {
  return x / (((1 / b - 2) * (1 - x)) + 1);
}

function getSchlickBias(g, x) {
  if (x < 0.5) {
    x *= 2;
    return 0.5 * getBias(g, x);
  }

  x = 2 * x - 1;
  return 0.5 * getBias(1 - g, x) + 0.5;
}

function applyGainAndBias(value, gain, bias) {
  const b = clamp(bias, 0, 1);
  const g = clamp(gain, 0, 1);

  if (value > 0.999) {
    return 1;
  }
  if (value < 0.00001) {
    return 0;
  }
  if (g < 0.5) {
    return getSchlickBias(g, getBias(b, value));
  }
  return getBias(b, getSchlickBias(g, value));
}

function fmod(value, mod) {
  return value - mod * Math.floor(value / mod);
}

function tixlRemap(value, rangeInMin, rangeInMax, rangeOutMin, rangeOutMax, biasAndGain, mode) {
  let normalized = (value - rangeInMin) / (rangeInMax - rangeInMin);
  if (normalized > 0 && normalized < 1) {
    normalized = applyGainAndBias(normalized, biasAndGain.x, biasAndGain.y);
  }

  let result = normalized * (rangeOutMax - rangeOutMin) + rangeOutMin;
  if (mode === 1) {
    result = clamp(result, Math.min(rangeOutMin, rangeOutMax), Math.max(rangeOutMin, rangeOutMax));
  } else if (mode === 2) {
    const min = Math.min(rangeOutMin, rangeOutMax);
    const max = Math.max(rangeOutMin, rangeOutMax);
    result = fmod(result, max - min);
  }
  return result;
}

function tixlLerp(a, b, f, shouldClamp) {
  if (shouldClamp) {
    f = clamp(f, 0, 1);
  }
  return a + (b - a) * f;
}

function closeTo(actual, expected, message) {
  assert.ok(Math.abs(actual - expected) < 1e-9, `${message}: ${actual} !== ${expected}`);
}

test("TiXL Remap scalar reference cases", () => {
  closeTo(tixlRemap(0.25, 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 0), 12.5, "normal range");
  closeTo(tixlRemap(2, 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 0), 30, "normal extrapolates");
  closeTo(tixlRemap(2, 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 1), 20, "clamped mode limits high");
  closeTo(tixlRemap(-0.5, 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 1), 10, "clamped mode limits low");
  closeTo(tixlRemap(1.25, 0, 1, 10, 20, { x: 0.5, y: 0.5 }, 2), 2.5, "modulo mode applies TiXL Fmod to output value");
  assert.notEqual(tixlRemap(0.25, 0, 1, 10, 20, { x: 0.2, y: 0.8 }, 0), 12.5, "gain/bias changes interior values");
});

test("TiXL Lerp scalar reference cases", () => {
  closeTo(tixlLerp(0, 1, 0, false), 0, "default returns A");
  closeTo(tixlLerp(10, 20, 0.25, false), 12.5, "interpolates inside range");
  closeTo(tixlLerp(10, 20, 1.5, false), 25, "extrapolates when unclamped");
  closeTo(tixlLerp(10, 20, 1.5, true), 20, "clamps high factor");
  closeTo(tixlLerp(10, 20, -0.5, true), 10, "clamps low factor");
});
