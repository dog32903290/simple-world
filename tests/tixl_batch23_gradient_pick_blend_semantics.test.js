#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a = 1) {
  return { r, g, b, a };
}

function gradient(steps, interpolation = 0) {
  return { steps: steps.slice().sort((a, b) => a.position - b.position), interpolation };
}

function closeColor(actual, expected) {
  for (const key of ["r", "g", "b", "a"]) {
    assert.ok(Math.abs(actual[key] - expected[key]) < 1e-6, `${key}: ${actual[key]} != ${expected[key]}`);
  }
}

function positiveModulo(value, repeat) {
  if (repeat === 0) return 0;
  const x = value % repeat;
  return x < 0 ? repeat + x : x;
}

function lerpColor(a, b, t) {
  return color(
    a.r + (b.r - a.r) * t,
    a.g + (b.g - a.g) * t,
    a.b + (b.b - a.b) * t,
    a.a + (b.a - a.a) * t,
  );
}

function sampleGradient(g, t) {
  const samplePos = Math.min(Math.max(t, 0), 1);
  let previous = null;
  for (const step of g.steps) {
    if (!(step.position >= samplePos)) {
      previous = step;
      continue;
    }
    if (previous === null || previous.position >= step.position) return step.color;
    const fraction = Math.min(Math.max((samplePos - previous.position) / (step.position - previous.position), 0), 1);
    return lerpColor(previous.color, step.color, fraction);
  }
  return previous ? previous.color : color(1, 1, 1, 1);
}

function pickGradient(gradients, index, previousSelected = null) {
  if (!gradients || gradients.length === 0) return previousSelected;
  return gradients[positiveModulo(index, gradients.length)];
}

function blendColor(a, b, mode, mixFactor) {
  if (mode === 0) {
    return color(
      (1 - b.a) * a.r + b.a * b.r,
      (1 - b.a) * a.g + b.a * b.g,
      (1 - b.a) * a.b + b.a * b.b,
      a.a + b.a - a.a * b.a,
    );
  }
  if (mode === 1) {
    return color(a.r * b.r, a.g * b.g, a.b * b.b, a.a + b.a - a.a * b.a);
  }
  if (mode === 2) {
    return color(1 - (1 - a.r) * (1 - b.r), 1 - (1 - a.g) * (1 - b.g), 1 - (1 - a.b) * (1 - b.b), a.a + b.a - a.a * b.a);
  }
  if (mode === 3) {
    return lerpColor(a, b, mixFactor);
  }
  return color(1, 1, 1, 1);
}

function blendGradients(a, b, mode, mixFactor) {
  const clampedMix = Math.min(Math.max(mixFactor, 0), 1);
  const byPosition = new Map();
  for (const step of a.steps) {
    byPosition.set(step.position, blendColor(step.color, sampleGradient(b, step.position), mode, clampedMix));
  }
  for (const step of b.steps) {
    byPosition.set(step.position, blendColor(sampleGradient(a, step.position), step.color, mode, clampedMix));
  }
  return gradient([...byPosition.entries()].map(([position, c]) => ({ position, color: c })), 0);
}

test("TiXL PickGradient selects connected gradient by positive modulo and preserves previous output for no connections", () => {
  const g0 = gradient([{ position: 0, color: color(1, 0, 0) }], 0);
  const g1 = gradient([{ position: 0, color: color(0, 1, 0) }], 2);
  const g2 = gradient([{ position: 0, color: color(0, 0, 1) }], 3);

  assert.equal(pickGradient([g0, g1, g2], -1), g2);
  assert.equal(pickGradient([g0, g1, g2], 4), g1);
  assert.equal(pickGradient([], 0, g0), g0);
});

test("TiXL BlendGradients blends the union of A/B step positions and outputs Linear interpolation", () => {
  const a = gradient([
    { position: 0, color: color(1, 0, 0, 1) },
    { position: 1, color: color(0, 0, 1, 1) },
  ], 0);
  const b = gradient([
    { position: 0.5, color: color(0, 1, 0, 0.5) },
    { position: 1, color: color(1, 1, 0, 0.25) },
  ], 0);

  const normal = blendGradients(a, b, 0, 0);
  assert.deepEqual(normal.steps.map((step) => step.position), [0, 0.5, 1]);
  assert.equal(normal.interpolation, 0);
  closeColor(normal.steps[0].color, color(0.5, 0.5, 0, 1));
  closeColor(normal.steps[1].color, color(0.25, 0.5, 0.25, 1));
  closeColor(normal.steps[2].color, color(0.25, 0.25, 0.75, 1));

  const mix = blendGradients(a, b, 3, 0.25);
  closeColor(mix.steps[1].color, color(0.375, 0.25, 0.375, 0.875));
});
