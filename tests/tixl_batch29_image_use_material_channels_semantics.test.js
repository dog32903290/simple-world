#!/usr/bin/env node

const assert = require("node:assert/strict");
const test = require("node:test");

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function selectedChannel(a, b, c, select) {
  const source = select < 5 ? a : select < 10 ? b : c;
  const mode = select % 5;
  if (mode < 3) return source[mode];
  if (mode === 3) return (source[0] + source[1] + source[2]) / 3;
  return clamp(0.239 * source[0] + 0.686 * source[1] + 0.075 * source[2], 0, 1);
}

function combineMaterialChannels2(a, colorA, b, colorB, c, colorC, selectR, selectG, selectB, alphaMode) {
  const ta = a.map((channel, index) => channel * colorA[index]);
  const tb = b.map((channel, index) => channel * colorB[index]);
  const tc = c.map((channel, index) => channel * colorC[index]);
  const alpha = alphaMode === 0 ? ta[3] : alphaMode === 1 ? tb[3] : alphaMode === 2 ? tc[3] : alphaMode === 3 ? 0 : 1;
  return [
    selectedChannel(ta, tb, tc, selectR),
    selectedChannel(ta, tb, tc, selectG),
    selectedChannel(ta, tb, tc, selectB),
    alpha,
  ];
}

function combineMaterialChannels(roughness, metallic, occlusion, connected = { roughness: true, metallic: true, occlusion: true }) {
  return [
    connected.roughness ? roughness[0] : 0.5,
    connected.metallic ? metallic[1] : 0,
    connected.occlusion ? occlusion[0] : 1,
    1,
  ];
}

test("CombineMaterialChannels2 preserves Combine3Images channel selection law", () => {
  const red = [1, 0, 0, 1];
  const green = [0, 1, 0, 1];
  const blue = [0, 0, 1, 1];
  const white = [1, 1, 1, 1];

  assert.deepEqual(combineMaterialChannels2(red, white, green, white, blue, white, 0, 6, 12, 4), [1, 1, 1, 1]);
});

test("CombineMaterialChannels packs roughness.r metallic.g occlusion.r with TiXL fallback values", () => {
  assert.deepEqual(
    combineMaterialChannels([0.25, 0, 0, 1], [0, 0.75, 0, 1], [0.5, 0, 0, 1]),
    [0.25, 0.75, 0.5, 1]
  );
  assert.deepEqual(
    combineMaterialChannels([0, 0, 0, 1], [0, 0, 0, 1], [0, 0, 0, 1], { roughness: false, metallic: false, occlusion: false }),
    [0.5, 0, 1, 1]
  );
});
