const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch 45 proof wires Lib.numbers.anim.time nodes into a visible save path", () => {
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-45-anim-time-proof.vuo"), "utf8");
  for (const title of ["my_AbletonLinkSync", "my_ClipTime", "my_ConvertTime", "my_DateTimeInSecs", "my_GetFrameSpeedFactor", "my_HasTimeChanged", "my_LastFrameDuration", "my_RunTime", "my_SetPlaybackSpeed", "my_SetPlaybackTime", "my_SetTime", "my_StopWatch", "my_Time", "my_Batch45AnimTimeProof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-45-anim-time-vuo-save/);
  assert.match(s, /ProofValue/);
});
