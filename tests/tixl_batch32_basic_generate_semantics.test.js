#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function checkerAt(x, y) {
  const ax = x - Math.floor(x);
  const ay = y - Math.floor(y);
  return (ax > 0.5 && ay < 0.5) || (ax < 0.5 && ay > 0.5) ? 0 : 1;
}

function pingPongRepeat(x, pingPong, repeat) {
  const baseValue = x + 0.5;
  const repeatValue = baseValue - Math.floor(baseValue);
  const pingPongValue = 1 - Math.abs(((x * 0.5) - Math.floor(x * 0.5)) * 2 - 1);
  const singlePingPong = Math.abs(x);
  let value = repeat ? repeatValue : baseValue;
  if (pingPong) value = repeat ? pingPongValue : singlePingPong;
  return repeat ? value : Math.min(1, Math.max(0, value));
}

function radialDistance(x, y, stretch = [1, 1], width = 1) {
  return Math.hypot(x / stretch[0], y / stretch[1]) * 2 / Math.max(Math.abs(width), 0.000001) - 0.5;
}

function roundedRectSignedDistance(x, y, sx, sy, scale, round) {
  const size = [sx * scale, sy * scale];
  const minSize = Math.min(size[0], size[1]);
  const roundOffset = minSize * round;
  const b = [(size[0] - roundOffset) / 2, (size[1] - roundOffset) / 2];
  const dx = Math.abs(x) - b[0];
  const dy = Math.abs(y) - b[1];
  return Math.hypot(Math.max(dx, 0), Math.max(dy, 0)) + Math.min(Math.max(dx, dy), 0);
}

test("CheckerBoard follows TiXL alternating-cell rule", () => {
  assert.equal(checkerAt(0.25, 0.25), 1);
  assert.equal(checkerAt(0.75, 0.25), 0);
  assert.equal(checkerAt(0.25, 0.75), 0);
  assert.equal(checkerAt(0.75, 0.75), 1);
});

test("Linear/Radial gradient PingPongRepeat matches TiXL branch shape", () => {
  assert.equal(pingPongRepeat(0.25, false, false), 0.75);
  assert.equal(pingPongRepeat(1.25, false, true), 0.75);
  assert.equal(pingPongRepeat(-0.25, true, false), 0.25);
  assert.equal(pingPongRepeat(1.25, true, true), 0.75);
});

test("RadialGradient derives center-to-edge value from stretched distance", () => {
  assert.equal(radialDistance(0, 0), -0.5);
  assert.ok(radialDistance(0.5, 0.0) > radialDistance(0.25, 0.0));
  assert.ok(radialDistance(0.0, 0.5, [1, 0.5]) > radialDistance(0.0, 0.5, [1, 1]));
});

test("RoundedRect signed distance separates inside and outside", () => {
  assert.ok(roundedRectSignedDistance(0, 0, 1, 1, 0.5, 0.5) < 0);
  assert.ok(roundedRectSignedDistance(1, 1, 1, 1, 0.5, 0.5) > 0);
});
