const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch 45 Lib.numbers.anim.time source namespace is audited", () => {
  for (const name of ["AbletonLinkSync", "ClipTime", "ConvertTime", "DateTimeInSecs", "GetFrameSpeedFactor", "HasTimeChanged", "LastFrameDuration", "RunTime", "SetPlaybackSpeed", "SetPlaybackTime", "SetTime", "StopWatch", "Time"]) {
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/time/${name}.cs`), new RegExp(`class ${name}|sealed class ${name}`));
    assert.match(read(`external/tixl/Operators/Lib/numbers/anim/time/${name}.t3`), /DefaultValue|Inputs|Children|Id/);
  }
});
