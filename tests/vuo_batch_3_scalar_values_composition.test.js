#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-3-scalar-values-proof.vuo");

test("Batch 3 scalar values proof wires bool and float-adjust nodes into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  const requiredNodes = [
    "my_And",
    "my_Or",
    "my_Not",
    "my_Xor",
    "my_BoolToFloat",
    "my_BoolToInt",
    "my_PickBool",
    "my_Abs",
    "my_Ceil",
    "my_Floor",
    "my_Round",
    "my_InvertFloat",
    "my_Clamp",
    "my_Add",
    "my_Sub",
    "my_Multiply",
    "my_Div",
    "my_Modulo",
    "my_Pow",
    "my_Sqrt",
  ];

  for (const title of requiredNodes) {
    assert.match(source, new RegExp(title));
  }

  assert.match(source, /my_Batch3ScalarValuesProof/);
  assert.match(source, /vuo\.image\.render\.window2/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /And:result -> ProofImage:andValue/);
  assert.match(source, /BoolToFloat:result -> ProofImage:boolToFloatValue/);
  assert.match(source, /PickBool:selected -> ProofImage:pickBoolValue/);
  assert.match(source, /Abs:result -> ProofImage:absValue/);
  assert.match(source, /Round:result -> ProofImage:roundValue/);
  assert.match(source, /Clamp:result -> ProofImage:clampValue/);
  assert.match(source, /Add:result -> ProofImage:addValue/);
  assert.match(source, /Sub:result -> ProofImage:subValue/);
  assert.match(source, /Multiply:result -> ProofImage:multiplyValue/);
  assert.match(source, /Div:result -> ProofImage:divValue/);
  assert.match(source, /Modulo:result -> ProofImage:moduloValue/);
  assert.match(source, /Pow:result -> ProofImage:powValue/);
  assert.match(source, /Sqrt:result -> ProofImage:sqrtValue/);
});

test("Batch 3 proof adapter exposes one visual input per manufactured node output", () => {
  const source = fs.readFileSync(path.join(repoRoot, "vuo-nodes/my.numbers.batch.scalarValuesProof.c"), "utf8");
  for (const input of [
    "andValue",
    "orValue",
    "notValue",
    "xorValue",
    "boolToFloatValue",
    "boolToIntValue",
    "pickBoolValue",
    "absValue",
    "ceilValue",
    "floorValue",
    "roundValue",
    "invertFloatValue",
    "clampValue",
    "addValue",
    "subValue",
    "multiplyValue",
    "divValue",
    "moduloValue",
    "powValue",
    "sqrtValue",
  ]) {
    assert.match(source, new RegExp(`\\b${input}\\b`));
  }
  assert.match(source, /Proof-only Vuo image adapter for Batch 3 scalar value nodes/);
  assert.match(source, /float values\[20\]/);
});
