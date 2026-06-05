#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a) {
  return { r, g, b, a };
}

function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

function toGamma(c) {
  return color(Math.pow(c.r, 1 / 2.2), Math.pow(c.g, 1 / 2.2), Math.pow(c.b, 1 / 2.2), c.a);
}

function okLabToRgba(lab) {
  const l1 = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const m1 = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const s1 = lab.l - 0.0894841775 * lab.a - 1.291485548 * lab.b;
  const l = l1 * l1 * l1;
  const m = m1 * m1 * m1;
  const s = s1 * s1 * s1;
  return color(
    +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s,
    lab.alpha,
  );
}

function fromOkLch(lightness, chroma, hueNormalized, alpha, intensityBoost) {
  const h = (hueNormalized % 1) * 360 * (Math.PI / 180);
  const lab = {
    l: clamp(lightness, 0, 1),
    a: chroma * Math.cos(h),
    b: chroma * Math.sin(h),
    alpha,
  };
  const hdrExcess = Math.max(0, lightness - 1);
  const linear = okLabToRgba(lab);
  const srgb = toGamma(color(clamp(linear.r, 0, 1), clamp(linear.g, 0, 1), clamp(linear.b, 0, 1), linear.a));
  const hdrScale = hdrExcess > 0 ? 1 + hdrExcess : 1;
  return color(srgb.r * hdrScale * intensityBoost, srgb.g * hdrScale * intensityBoost, srgb.b * hdrScale * intensityBoost, srgb.a);
}

function combineColorLists(lists) {
  const output = [];
  if (!lists || lists.length === 0) return output;
  for (const list of lists) {
    if (list && list.length > 0) output.push(...list);
  }
  return output;
}

function closeColor(actual, expected) {
  for (const key of ["r", "g", "b", "a"]) {
    assert.ok(Math.abs(actual[key] - expected[key]) < 1e-5, `${key}: ${actual[key]} != ${expected[key]}`);
  }
}

test("TiXL OKLChToColor converts OKLCh to gamma RGB and applies intensity boost to RGB only", () => {
  const gray = fromOkLch(0.5, 0, 0, 0.75, 2);
  closeColor(gray, color(0.777203, 0.777203, 0.777203, 0.75));

  const colored = fromOkLch(0.7, 0.12, 0.25, 0.8, 1);
  closeColor(colored, color(0.729094, 0.600508, 0.235208, 0.8));
});

test("TiXL CombineColorLists concatenates connected non-empty color lists in input order", () => {
  const red = color(1, 0, 0, 1);
  const green = color(0, 1, 0, 1);
  const blue = color(0, 0, 1, 1);
  assert.deepEqual(combineColorLists([[red], [], null, [green, blue]]), [red, green, blue]);
  assert.deepEqual(combineColorLists([]), []);
});
