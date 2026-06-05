const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 54 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.anim.animators.animBoolean.c", "my_AnimBoolean", "AnimBoolean"],
    ["vuo-nodes/my.numbers.anim.animators.animFloatList.c", "my_AnimFloatList", "AnimFloatList"],
    ["vuo-nodes/my.numbers.anim.animators.animInt.c", "my_AnimInt", "AnimInt"],
    ["vuo-nodes/my.numbers.anim.animators.animValue.c", "my_AnimValue", "AnimValue"],
    ["vuo-nodes/my.numbers.anim.animators.animVec2.c", "my_AnimVec2", "AnimVec2"],
    ["vuo-nodes/my.numbers.anim.animators.animVec3.c", "my_AnimVec3", "AnimVec3"],
    ["vuo-nodes/my.numbers.anim.animators.oscillateVec2.c", "my_OscillateVec2", "OscillateVec2"],
    ["vuo-nodes/my.numbers.anim.animators.oscillateVec3.c", "my_OscillateVec3", "OscillateVec3"],
    ["vuo-nodes/my.numbers.anim.animators.sequenceAnim.c", "my_SequenceAnim", "SequenceAnim"],
    ["vuo-nodes/my.numbers.anim.animators.triggerAnim.c", "my_TriggerAnim", "TriggerAnim"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/anim/animators/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/anim\/animators/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
