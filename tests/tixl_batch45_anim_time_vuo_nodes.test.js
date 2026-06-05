const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 45 Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {
  const nodes = [
    ["vuo-nodes/my.numbers.anim.time.abletonLinkSync.c", "my_AbletonLinkSync", "AbletonLinkSync"],
    ["vuo-nodes/my.numbers.anim.time.clipTime.c", "my_ClipTime", "ClipTime"],
    ["vuo-nodes/my.numbers.anim.time.convertTime.c", "my_ConvertTime", "ConvertTime"],
    ["vuo-nodes/my.numbers.anim.time.dateTimeInSecs.c", "my_DateTimeInSecs", "DateTimeInSecs"],
    ["vuo-nodes/my.numbers.anim.time.getFrameSpeedFactor.c", "my_GetFrameSpeedFactor", "GetFrameSpeedFactor"],
    ["vuo-nodes/my.numbers.anim.time.hasTimeChanged.c", "my_HasTimeChanged", "HasTimeChanged"],
    ["vuo-nodes/my.numbers.anim.time.lastFrameDuration.c", "my_LastFrameDuration", "LastFrameDuration"],
    ["vuo-nodes/my.numbers.anim.time.runTime.c", "my_RunTime", "RunTime"],
    ["vuo-nodes/my.numbers.anim.time.setPlaybackSpeed.c", "my_SetPlaybackSpeed", "SetPlaybackSpeed"],
    ["vuo-nodes/my.numbers.anim.time.setPlaybackTime.c", "my_SetPlaybackTime", "SetPlaybackTime"],
    ["vuo-nodes/my.numbers.anim.time.setTime.c", "my_SetTime", "SetTime"],
    ["vuo-nodes/my.numbers.anim.time.stopWatch.c", "my_StopWatch", "StopWatch"],
    ["vuo-nodes/my.numbers.anim.time.time.c", "my_Time", "Time"]
  ];
  for (const [file, title, donor] of nodes) {
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\s*:\\s*"${title}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/numbers/anim/time/${donor}.cs`));
    assert.match(s, /Category: Operators\/Lib\/numbers\/anim\/time/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }
});
