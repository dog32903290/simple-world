#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function hsbToColor(hue, saturation, brightness, alpha) {
  let h = hue % 360;
  let r = 0;
  let g = 0;
  let b = 0;

  if (saturation === 0) {
    r = brightness;
    g = brightness;
    b = brightness;
  } else {
    h %= 360;
    if (h < 0) h += 360;
    const sector = Math.trunc(h / 60);
    const fractional = h / 60 - sector;
    const p = brightness * (1 - saturation);
    const q = brightness * (1 - saturation * fractional);
    const t = brightness * (1 - saturation * (1 - fractional));
    if (sector === 0) [r, g, b] = [brightness, t, p];
    else if (sector === 1) [r, g, b] = [q, brightness, p];
    else if (sector === 2) [r, g, b] = [p, brightness, t];
    else if (sector === 3) [r, g, b] = [p, q, brightness];
    else if (sector === 4) [r, g, b] = [t, p, brightness];
    else if (sector === 5) [r, g, b] = [brightness, p, q];
  }

  return { r, g, b, a: alpha };
}

function hslToColor(hue, saturation, lightness, alpha) {
  const h = (hue % 1) * 360;
  let satR = 1;
  let satG = 1;
  let satB = 1;

  if (h < 120) {
    satR = (120 - h) / 60;
    satG = h / 60;
    satB = 0;
  } else if (h < 240) {
    satR = 0;
    satG = (240 - h) / 60;
    satB = (h - 120) / 60;
  } else {
    satR = (h - 240) / 60;
    satG = 0;
    satB = (360 - h) / 60;
  }

  satR = satR < 1 ? satR : 1;
  satG = satG < 1 ? satG : 1;
  satB = satB < 1 ? satB : 1;

  const tmpR = 2 * saturation * satR + (1 - saturation);
  const tmpG = 2 * saturation * satG + (1 - saturation);
  const tmpB = 2 * saturation * satB + (1 - saturation);

  if (lightness < 0.5) {
    return { r: lightness * tmpR, g: lightness * tmpG, b: lightness * tmpB, a: alpha };
  }
  return {
    r: (1 - lightness) * tmpR + 2 * lightness - 1,
    g: (1 - lightness) * tmpG + 2 * lightness - 1,
    b: (1 - lightness) * tmpB + 2 * lightness - 1,
    a: alpha,
  };
}

function colorsToList(colors) {
  return colors ? [...colors] : [];
}

function closeColor(actual, expected) {
  for (const key of ["r", "g", "b", "a"]) {
    assert.ok(Math.abs(actual[key] - expected[key]) < 1e-6, `${key}: ${actual[key]} != ${expected[key]}`);
  }
}

test("TiXL HSBToColor converts hue degrees, saturation, brightness, and alpha", () => {
  closeColor(hsbToColor(120, 1, 0.5, 0.25), { r: 0, g: 0.5, b: 0, a: 0.25 });
  closeColor(hsbToColor(-60, 1, 1, 1), { r: 1, g: 0, b: 1, a: 1 });
  closeColor(hsbToColor(90, 0, 0.42, 0.8), { r: 0.42, g: 0.42, b: 0.42, a: 0.8 });
});

test("TiXL HSLToColor uses hue as a normalized cycle and preserves TiXL's custom formula", () => {
  closeColor(hslToColor(1 / 3, 1, 0.5, 1), { r: 0, g: 1, b: 0, a: 1 });
  closeColor(hslToColor(0, 0, 0.5, 0.7), { r: 0.5, g: 0.5, b: 0.5, a: 0.7 });
});

test("TiXL ColorsToList collects connected color inputs in order", () => {
  const colors = [{ r: 1, g: 0, b: 0, a: 1 }, { r: 0, g: 1, b: 0, a: 0.5 }];
  assert.deepEqual(colorsToList(colors), colors);
});
