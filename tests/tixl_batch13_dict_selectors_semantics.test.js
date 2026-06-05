#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function parseFloatDict(text) {
  const dict = new Map();
  for (const rawPart of String(text ?? "").split(/[,\n;]/)) {
    const part = rawPart.trim();
    if (!part) continue;
    const separator = part.includes("=") ? "=" : ":";
    const index = part.indexOf(separator);
    if (index < 0) continue;
    const key = part.slice(0, index).trim();
    const value = Number(part.slice(index + 1).trim());
    if (key && Number.isFinite(value)) dict.set(key, value);
  }
  return dict;
}

function selectFloat(dictText, select, previous = 0) {
  const dict = parseFloatDict(dictText);
  return dict.has(select) ? dict.get(select) : previous;
}

function selectBool(dictText, select, previous = false) {
  const dict = parseFloatDict(dictText);
  return dict.has(select) ? dict.get(select) > 0.5 : previous;
}

function selectVec2(dictText, selectX, previous = { x: 0, y: 0 }) {
  const dict = parseFloatDict(dictText);
  const keys = [...dict.keys()].sort();
  const xIndex = keys.indexOf(selectX);
  const yKey = xIndex >= 0 ? keys[xIndex + 1] : undefined;
  if (yKey && dict.has(selectX) && dict.has(yKey)) {
    return { x: dict.get(selectX), y: dict.get(yKey) };
  }
  return previous;
}

function differentChars(a, b) {
  if (a.length !== b.length) return Number.MAX_SAFE_INTEGER;
  let count = 0;
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) count++;
  }
  return count;
}

function selectVec3(dictText, selectX, previous = { x: 0, y: 0, z: 0 }) {
  const dict = parseFloatDict(dictText);
  const keys = [...dict.keys()].sort();
  const start = keys.indexOf(selectX);
  if (start < 0) return previous;
  const vectorKeys = [];
  for (let i = start; i < keys.length && vectorKeys.length < 3; i++) {
    if (differentChars(keys[i], selectX) <= 1) vectorKeys.push(keys[i]);
    else break;
  }
  if (vectorKeys.length !== 3) return previous;
  return {
    x: dict.get(vectorKeys[0]),
    y: dict.get(vectorKeys[1]),
    z: dict.get(vectorKeys[2]),
  };
}

test("TiXL SelectFloatFromDict returns selected float and keeps previous result for missing keys", () => {
  assert.equal(selectFloat("axis.x=0.25; trigger=0.75", "trigger"), 0.75);
  assert.equal(selectFloat("axis.x=0.25", "missing", 9), 9);
});

test("TiXL SelectBoolFromFloatDict uses a strict 0.5 threshold and keeps previous result for missing keys", () => {
  assert.equal(selectBool("a=0.5; b=0.5001; c=1", "a", true), false);
  assert.equal(selectBool("a=0.5; b=0.5001; c=1", "b"), true);
  assert.equal(selectBool("a=0.5", "missing", true), true);
});

test("TiXL SelectVec2FromDict uses the sorted key immediately after SelectX as Y", () => {
  assert.deepEqual(selectVec2("joy.y=2; joy.x=1; z=9", "joy.x"), { x: 1, y: 2 });
  assert.deepEqual(selectVec2("a=1; c=3", "c", { x: 8, y: 9 }), { x: 8, y: 9 });
});

test("TiXL SelectVec3FromDict accepts three sorted keys whose names differ from SelectX by at most one char", () => {
  assert.deepEqual(selectVec3("rot.Z=30; rot.X=10; rot.Y=20; other=99", "rot.X"), { x: 10, y: 20, z: 30 });
  assert.deepEqual(selectVec3("posX=1; posY=2; posLong=3", "posX", { x: 7, y: 8, z: 9 }), { x: 7, y: 8, z: 9 });
});
