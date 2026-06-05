#!/usr/bin/env node

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const compositionPath = path.join(repoRoot, "vuo-compositions/generated/myworld-batch-2-grade-a-strings-proof.vuo");

test("Batch 2 strings proof wires every manufactured node into a visible image adapter", () => {
  const source = fs.readFileSync(compositionPath, "utf8");
  const requiredNodes = [
    "my_FloatToString",
    "my_IntToString",
    "my_IndexOf",
    "my_SearchAndReplace",
    "my_SubString",
    "my_StringLength",
    "my_StringRepeat",
    "my_ChangeCase",
    "my_SplitString",
    "my_JoinStringList",
  ];

  for (const title of requiredNodes) {
    assert.match(source, new RegExp(title));
  }

  assert.match(source, /my_Batch2GradeAStringsProof/);
  assert.match(source, /vuo\.image\.render\.window2/);
  assert.match(source, /ProofImage:image -> RenderWindow:image/);
  assert.match(source, /FloatToString:output -> ProofImage:floatText/);
  assert.match(source, /IndexOf:index -> ProofImage:indexValue/);
  assert.match(source, /ChangeCase:result -> ProofImage:changeCaseText/);
  assert.match(source, /SplitString:fragments -> JoinStringList:input/);
  assert.match(source, /SplitString:fragments -> ProofImage:splitFragments/);
  assert.match(source, /SplitString:count -> ProofImage:splitCount/);
  assert.match(source, /JoinStringList:result -> ProofImage:joinedText/);
});
