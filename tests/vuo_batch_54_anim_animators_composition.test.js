const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 54 proof wires Lib.numbers.anim.animators nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-54-anim-animators-proof.vuo"), "utf8");
  for (const title of ["my_AnimBoolean", "my_AnimFloatList", "my_AnimInt", "my_AnimValue", "my_AnimVec2", "my_AnimVec3", "my_OscillateVec2", "my_OscillateVec3", "my_SequenceAnim", "my_TriggerAnim", "my_Batch54AnimAnimatorsProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-54-anim-animators-vuo-save/);
  assert.match(s, /ProofValue/);
});
