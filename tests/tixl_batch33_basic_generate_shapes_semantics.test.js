#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function blobFalloff(distance, scale, feather) {
  const f = feather * scale / 2;
  const low = scale / 2 - f;
  const high = scale / 2 + f;
  if (distance <= low) return 0;
  if (distance >= high) return 1;
  const t = (distance - low) / (high - low);
  return t * t * (3 - 2 * t);
}

function pingPongRepeat(x, pingPong, repeat) {
  const repeatValue = x - Math.floor(x);
  const pingPongValue = 1 - Math.abs(((x * 0.5) - Math.floor(x * 0.5)) * 2 - 1);
  const singlePingPong = Math.abs(x);
  let value = repeat ? repeatValue : x;
  if (pingPong) value = repeat ? pingPongValue : singlePingPong;
  return repeat ? value : Math.min(1, Math.max(0, value));
}

function roundedBoxDistance(x, y, bx, by, r) {
  const qx = Math.abs(x) - bx + r;
  const qy = Math.abs(y) - by + r;
  return Math.min(Math.max(qx, qy), 0) + Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) - r;
}

function nGonSectorAngle(sides) {
  return (Math.PI * 2) / Math.max(sides, 3);
}

test("Blob preserves circular falloff from fill toward background", () => {
  assert.equal(blobFalloff(0, 0.5, 1), 0);
  assert.equal(blobFalloff(0.5, 0.5, 1), 1);
  assert.ok(blobFalloff(0.25, 0.5, 1) > 0);
});

test("BoxGradient uses TiXL PingPongRepeat without the LinearGradient +0.5 offset", () => {
  assert.equal(pingPongRepeat(0.25, false, false), 0.25);
  assert.equal(pingPongRepeat(1.25, false, true), 0.25);
  assert.equal(pingPongRepeat(-0.25, true, false), 0.25);
});

test("BoxGradient rounded-box distance separates center from outside", () => {
  assert.ok(roundedBoxDistance(0, 0, 0.25, 0.25, 0) < 0);
  assert.ok(roundedBoxDistance(1, 1, 0.25, 0.25, 0) > 0);
});

test("NGon and NGonGradient clamp sides to regular-polygon sectors", () => {
  assert.equal(nGonSectorAngle(2), (Math.PI * 2) / 3);
  assert.equal(nGonSectorAngle(5), (Math.PI * 2) / 5);
});
