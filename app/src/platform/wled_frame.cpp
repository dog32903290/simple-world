// platform/wled_frame.cpp — Adalight frame builder, 1:1 with WLedSerialOutput.cs (ground-truth lines
// cited in wled_frame.h). Pure function, no I/O — the closed-form golden meat of the WLED node.
#include "platform/wled_frame.h"

namespace sw {
namespace {
constexpr int kAdalightHeaderSize = 6;  // WLED :290
constexpr int kMaxLedCount = 4096;      // WLED :291
}  // namespace

uint8_t wledToByte(float v) {  // WLED :264-271
  if (v <= 0.f) return 0;
  if (v >= 1.f) return 255;
  return static_cast<uint8_t>(v * 255.f + 0.5f);
}

std::vector<uint8_t> buildWledFrame(const std::vector<WledColor>& colors, int ledCount,
                                    WledMapMode mapMode, WledColorOrder colorOrder, float brightness) {
  const int srcCount = static_cast<int>(colors.size());
  // ledCount<=0 → colors.Count; clamp to MaxLedCount. WLED :126-129.
  int leds = ledCount <= 0 ? srcCount : ledCount;
  if (leds > kMaxLedCount) leds = kMaxLedCount;
  if (leds < 0) leds = 0;

  // brightness clamp [0,1]. WLED :131-135.
  float b = brightness;
  if (b < 0.f) b = 0.f;
  else if (b > 1.f) b = 1.f;

  std::vector<uint8_t> buf(static_cast<size_t>(kAdalightHeaderSize + leds * 3), 0);

  // Adalight header. WLED :252-261 (uses ledCount-1 for the count field).
  const int countMinus1 = leds - 1;
  const uint8_t hi = static_cast<uint8_t>((countMinus1 >> 8) & 0xFF);
  const uint8_t lo = static_cast<uint8_t>(countMinus1 & 0xFF);
  buf[0] = 0x41;  // 'A'
  buf[1] = 0x64;  // 'd'
  buf[2] = 0x61;  // 'a'
  buf[3] = hi;
  buf[4] = lo;
  buf[5] = static_cast<uint8_t>(hi ^ lo ^ 0x55);

  if (leds == 0 || srcCount == 0) return buf;

  // color-order → r/g/b position in the on-wire triplet. WLED :146-155.
  int rPos, gPos, bPos;
  switch (colorOrder) {
    case WledColorOrder::RGB: rPos = 0; gPos = 1; bPos = 2; break;
    case WledColorOrder::GRB: rPos = 1; gPos = 0; bPos = 2; break;
    case WledColorOrder::BRG: rPos = 1; gPos = 2; bPos = 0; break;
    case WledColorOrder::RBG: rPos = 0; gPos = 2; bPos = 1; break;
    case WledColorOrder::BGR: rPos = 2; gPos = 1; bPos = 0; break;
    case WledColorOrder::GBR: rPos = 2; gPos = 0; bPos = 1; break;
    default:                  rPos = 1; gPos = 0; bPos = 2; break;  // GRB fallback (WLED :154)
  }

  // Precompute the lerp scale once. WLED :159-162.
  const float lerpScale =
      (mapMode == WledMapMode::Lerp && leds > 1) ? (srcCount - 1) / static_cast<float>(leds - 1) : 0.f;

  for (int i = 0; i < leds; ++i) {  // WLED :164-211
    float r, g, bb;
    if (mapMode == WledMapMode::Repeat) {
      const WledColor& c = colors[i % srcCount];
      r = c.r; g = c.g; bb = c.b;
    } else if (mapMode == WledMapMode::Lerp && srcCount > 1 && leds > 1) {
      const float t = i * lerpScale;
      const int loI = static_cast<int>(t);
      if (loI >= srcCount - 1) {
        const WledColor& c = colors[srcCount - 1];
        r = c.r; g = c.g; bb = c.b;
      } else {
        const float frac = t - loI;
        const WledColor& ca = colors[loI];
        const WledColor& cb = colors[loI + 1];
        r  = ca.r + (cb.r - ca.r) * frac;
        g  = ca.g + (cb.g - ca.g) * frac;
        bb = ca.b + (cb.b - ca.b) * frac;
      }
    } else {
      // Stretch (nearest-neighbor). WLED :197-205: srcIdx = i*src/led (long math), clamped.
      int srcIdx = static_cast<int>(static_cast<long long>(i) * srcCount / leds);
      if (srcIdx >= srcCount) srcIdx = srcCount - 1;
      const WledColor& c = colors[srcIdx];
      r = c.r; g = c.g; bb = c.b;
    }

    const int pixelStart = kAdalightHeaderSize + i * 3;
    buf[pixelStart + rPos] = wledToByte(r * b);
    buf[pixelStart + gPos] = wledToByte(g * b);
    buf[pixelStart + bPos] = wledToByte(bb * b);
  }

  return buf;
}

}  // namespace sw
