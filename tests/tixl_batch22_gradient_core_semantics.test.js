#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a = 1) {
  return { r, g, b, a };
}

function closeColor(actual, expected) {
  for (const key of ["r", "g", "b", "a"]) {
    assert.ok(Math.abs(actual[key] - expected[key]) < 1e-6, `${key}: ${actual[key]} != ${expected[key]}`);
  }
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function smootherStep(t) {
  const x = Math.min(Math.max(t, 0), 1);
  return x * x * x * (x * (x * 6 - 15) + 10);
}

function buildGradient(colors, positions, interpolation) {
  let gradientPositions = positions;
  if (!gradientPositions || gradientPositions.length === 0) {
    gradientPositions = colors.length === 1 ? [0] : colors.map((_, index) => index / (colors.length - 1));
  }

  const steps = colors.slice(0, Math.min(colors.length, gradientPositions.length))
    .map((c, index) => ({ position: gradientPositions[index], color: c }))
    .sort((a, b) => a.position - b.position);

  return { steps, interpolation };
}

function defineGradient(pairs, interpolation) {
  const steps = pairs
    .filter((pair) => pair.position >= 0)
    .map((pair) => ({ position: pair.position, color: pair.color }))
    .sort((a, b) => a.position - b.position);

  if (steps.length === 0) {
    steps.push({ position: 0, color: pairs[0].color });
  }

  return { steps, interpolation };
}

function sampleGradient(gradient, samplePos, overrideInterpolation = false, interpolation = 0) {
  const t = Math.min(Math.max(samplePos, 0), 1);
  const mode = overrideInterpolation ? interpolation : gradient.interpolation;
  let previousStep = null;

  for (const step of gradient.steps) {
    if (!(step.position >= t)) {
      previousStep = step;
      continue;
    }

    if (previousStep === null || previousStep.position >= step.position) {
      return step.color;
    }

    if (mode === 1) {
      return previousStep.color;
    }

    let fraction = (t - previousStep.position) / (step.position - previousStep.position);
    fraction = Math.min(Math.max(fraction, 0), 1);
    if (mode === 2) {
      fraction = smootherStep(fraction);
    }

    return color(
      lerp(previousStep.color.r, step.color.r, fraction),
      lerp(previousStep.color.g, step.color.g, fraction),
      lerp(previousStep.color.b, step.color.b, fraction),
      lerp(previousStep.color.a, step.color.a, fraction),
    );
  }

  return previousStep ? previousStep.color : color(1, 1, 1, 1);
}

test("TiXL BuildGradient falls back to normalized positions and sorts handles", () => {
  const gradient = buildGradient([color(1, 0, 0), color(0, 1, 0), color(0, 0, 1)], [], 3);
  assert.deepEqual(gradient.steps.map((step) => step.position), [0, 0.5, 1]);
  assert.equal(gradient.interpolation, 3);

  const sorted = buildGradient([color(0, 0, 1), color(1, 0, 0)], [1, 0], 0);
  assert.deepEqual(sorted.steps.map((step) => step.position), [0, 1]);
  closeColor(sorted.steps[0].color, color(1, 0, 0));
});

test("TiXL DefineGradient skips negative positions and falls back to Color1 at zero", () => {
  const gradient = defineGradient([
    { position: 1, color: color(1, 1, 1) },
    { position: -1, color: color(1, 0, 1, 0) },
    { position: 0.25, color: color(0, 1, 0) },
  ], 0);
  assert.deepEqual(gradient.steps.map((step) => step.position), [0.25, 1]);
  closeColor(gradient.steps[0].color, color(0, 1, 0));

  const fallback = defineGradient([
    { position: -1, color: color(0.2, 0.3, 0.4, 1) },
    { position: -1, color: color(1, 1, 1, 1) },
  ], 0);
  assert.deepEqual(fallback.steps.map((step) => step.position), [0]);
  closeColor(fallback.steps[0].color, color(0.2, 0.3, 0.4, 1));
});

test("TiXL SampleGradient clamps sample position and supports linear hold smooth overrides", () => {
  const gradient = buildGradient([color(1, 0, 0), color(0, 0, 1)], [0, 1], 0);
  closeColor(sampleGradient(gradient, 0.25), color(0.75, 0, 0.25, 1));
  closeColor(sampleGradient(gradient, 0.75, true, 1), color(1, 0, 0, 1));
  closeColor(sampleGradient(gradient, 0.5, true, 2), color(0.5, 0, 0.5, 1));
  closeColor(sampleGradient(gradient, -3), color(1, 0, 0, 1));
  closeColor(sampleGradient({ steps: [], interpolation: 0 }, 0.5), color(1, 1, 1, 1));
});
