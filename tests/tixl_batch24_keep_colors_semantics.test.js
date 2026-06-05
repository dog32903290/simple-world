#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function color(r, g, b, a = 1) {
  return { r, g, b, a };
}

function keepColorsStep(state, { newColor, addColor, maxLength, reset }) {
  const length = Math.min(Math.max(maxLength, 1), 100000);
  if (reset) state.length = 0;
  if (addColor) state.unshift(newColor);
  if (state.length > length) state.splice(length, state.length - length);
  return state;
}

test("TiXL KeepColors inserts new colors at the front and trims to clamped max length", () => {
  const state = [];
  keepColorsStep(state, { newColor: color(1, 0, 0), addColor: true, maxLength: 2, reset: false });
  keepColorsStep(state, { newColor: color(0, 1, 0), addColor: true, maxLength: 2, reset: false });
  keepColorsStep(state, { newColor: color(0, 0, 1), addColor: true, maxLength: 2, reset: false });

  assert.deepEqual(state, [color(0, 0, 1), color(0, 1, 0)]);
});

test("TiXL KeepColors preserves state when add is false and reset clears before optional add", () => {
  const state = [color(1, 0, 0), color(0, 1, 0)];
  keepColorsStep(state, { newColor: color(0, 0, 1), addColor: false, maxLength: 10, reset: false });
  assert.deepEqual(state, [color(1, 0, 0), color(0, 1, 0)]);

  keepColorsStep(state, { newColor: color(0.5, 0.5, 0.5), addColor: true, maxLength: 10, reset: true });
  assert.deepEqual(state, [color(0.5, 0.5, 0.5)]);

  keepColorsStep(state, { newColor: color(1, 1, 0), addColor: true, maxLength: 0, reset: false });
  assert.deepEqual(state, [color(1, 1, 0)]);
});
