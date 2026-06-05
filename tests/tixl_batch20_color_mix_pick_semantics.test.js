#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a) {
  return { r, g, b, a };
}

function blendColors(colorA, colorB, factor, mode) {
  if (mode === 0) {
    return color(
      colorA.r * (1 - factor) + colorB.r * factor,
      colorA.g * (1 - factor) + colorB.g * factor,
      colorA.b * (1 - factor) + colorB.b * factor,
      colorA.a * (1 - factor) + colorB.a * factor,
    );
  }
  if (mode === 1) {
    const fr = 1 * (1 - factor) + colorB.r * factor;
    const fg = 1 * (1 - factor) + colorB.g * factor;
    const fb = 1 * (1 - factor) + colorB.b * factor;
    const fa = 1 * (1 - factor) + colorB.a * factor;
    return color(colorA.r * fr, colorA.g * fg, colorA.b * fb, colorA.a * fa);
  }
  if (mode === 2) {
    return color(
      colorA.r + colorB.r * factor,
      colorA.g + colorB.g * factor,
      colorA.b + colorB.b * factor,
      colorA.a + colorB.a * factor,
    );
  }

  const result = color(
    (1 - colorB.a) * colorA.r + colorB.a * colorB.r,
    (1 - colorB.a) * colorA.g + colorB.a * colorB.g,
    (1 - colorB.a) * colorA.b + colorB.a * colorB.b,
    (1 - colorB.a) * colorA.a + colorB.a * colorB.a,
  );
  result.a = colorA.a + colorB.a - colorA.a * colorB.a;
  return result;
}

function positiveMod(value, mod) {
  const result = value % mod;
  return result < 0 ? result + mod : result;
}

function pickColorFromList(list, index, previous = color(0, 0, 0, 1)) {
  if (!list || list.length === 0) return previous;
  return list[positiveMod(index, list.length)];
}

function closeColor(actual, expected) {
  for (const key of ["r", "g", "b", "a"]) {
    assert.ok(Math.abs(actual[key] - expected[key]) < 1e-6, `${key}: ${actual[key]} != ${expected[key]}`);
  }
}

test("TiXL BlendColors supports Mix, Multiply, Add, and Blend modes", () => {
  const a = color(0.2, 0.4, 0.6, 0.5);
  const b = color(1.0, 0.5, 0.0, 0.25);
  closeColor(blendColors(a, b, 0.25, 0), color(0.4, 0.425, 0.45, 0.4375));
  closeColor(blendColors(a, b, 0.25, 1), color(0.2, 0.35, 0.45, 0.40625));
  closeColor(blendColors(a, b, 0.25, 2), color(0.45, 0.525, 0.6, 0.5625));
  closeColor(blendColors(a, b, 0.25, 3), color(0.4, 0.425, 0.45, 0.625));
});

test("TiXL PickColorFromList uses positive modulo and preserves previous output for empty input", () => {
  const colors = [color(1, 0, 0, 1), color(0, 1, 0, 1), color(0, 0, 1, 1)];
  assert.deepEqual(pickColorFromList(colors, -1), colors[2]);
  assert.deepEqual(pickColorFromList(colors, 4), colors[1]);
  assert.deepEqual(pickColorFromList([], 0, color(0.3, 0.4, 0.5, 0.6)), color(0.3, 0.4, 0.5, 0.6));
});
