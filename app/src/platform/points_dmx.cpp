// platform/points_dmx — impl. See points_dmx.h for spec + TiXL line citations.
#include "platform/points_dmx.h"

#include <algorithm>
#include <cmath>

namespace sw {

namespace {
float clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }
}  // namespace

std::vector<int> pointsToRgbList(const std::vector<float>& colorsRGB) {
  std::vector<int> out;
  const size_t triples = colorsRGB.size() / 3;
  out.reserve(triples * 3);
  for (size_t i = 0; i < triples; ++i) {
    // factor = (c - 0)/(1 - 0) = c; v = factor*255; Math.Round (PointsToRGBList.cs:98-106).
    // std::lround = half-away-from-zero (TiXL Math.Round(double) = banker's — goldens avoid .5).
    out.push_back((int)std::lround(colorsRGB[i * 3 + 0] * 255.0f));
    out.push_back((int)std::lround(colorsRGB[i * 3 + 1] * 255.0f));
    out.push_back((int)std::lround(colorsRGB[i * 3 + 2] * 255.0f));
  }
  return out;
}

int dmxValue8(float value, float inMin, float inMax) {
  float range = inMax - inMin;
  float normalized = clamp01((value - inMin) / range);        // cs:1268
  return (int)std::lround(normalized * 255.0f);               // cs:1269
}

int dmxValue16(float value, float inMin, float inMax) {
  float range = inMax - inMin;
  if (std::fabs(range) < 1e-4f) return 0;                     // cs:1281
  float normalized = (value - inMin) / range;                // cs:1282
  return (int)std::lround(clamp01(normalized) * 65535.0f);    // cs:1283
}

Dmx16Split dmxSplit16(float value, float inMin, float inMax) {
  int dmx16 = dmxValue16(value, inMin, inMax);
  return {(dmx16 >> 8) & 0xFF, dmx16 & 0xFF};                 // cs:1262-1263
}

}  // namespace sw
