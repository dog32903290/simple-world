// platform/points_dmx — pure closed-form value cores for the point→DMX/RGB converters
// (PointsToRGBList / PointsToDmxLights). These ops read a CPU point buffer (positions/colors/features)
// and emit an int list of DMX/RGB channel values. The GPU readback rail (the structured-buffer copy) is
// the same StructuredBufferReadAccess seam used by SampleCpuPoints (deferred/host-side); the load-bearing
// VALUE is the closed-form per-point conversion — the color→byte quantise (RGBList) and the SetDmxValue
// 8-bit / 16-bit channel map (DmxLights). Exposing them pure keeps the golden numeric-parity independent
// of the buffer transport.
//
// platform leaf: pure computation (std::vector<int>), no GPU/UI/runtime, no upward dep.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/dmx/obsolete/PointsToRGBList.cs:95-107  per-point Color.{X,Y,Z} → round(c*255) as
//     three list entries (R,G,B) in point order.
//   Operators/Lib/io/dmx/PointsToDMXLights.cs:1239-1284      SetDmxValue: 8-bit = round(clamp01((v-min)/
//     (max-min))*255); 16-bit = round(clamp01(...)*65535) split BIG-ENDIAN into coarse(hi)+fine(lo).
#pragma once
#include <cstdint>
#include <vector>

namespace sw {

// PointsToRGBList: for each point's linear color (r,g,b in 0..1), emit round(c*255) as three consecutive
// list entries (PointsToRGBList.cs:98-106). `colors` is the per-point RGB triples (size = 3·pointCount).
// Returns 3·pointCount ints. No clamping in the .cs (factor = (c-0)/(1-0) is the identity); values <0 or
// >1 round past 0..255 exactly as TiXL does.
std::vector<int> pointsToRgbList(const std::vector<float>& colorsRGB);

// SetDmxValue 8-bit mapping (PointsToDMXLights.cs:1265-1274): normalized = clamp01((value-inMin)/
// (inMax-inMin)); return round(normalized*255). Range collapse (|inMax-inMin|<1e-4) → the caller's guard
// applies; here inMax>inMin is assumed (the golden supplies a real range).
int dmxValue8(float value, float inMin, float inMax);

// MapToDmx16 (PointsToDMXLights.cs:1278-1284): |range|<1e-4 → 0; else round(clamp01((v-min)/range)*65535).
int dmxValue16(float value, float inMin, float inMax);

// SetDmxValue 16-bit channel split (PointsToDMXLights.cs:1262-1263): coarse byte = (dmx16>>8)&0xFF,
// fine byte = dmx16&0xFF (big-endian: coarse channel holds the HIGH byte).
struct Dmx16Split { int coarse, fine; };
Dmx16Split dmxSplit16(float value, float inMin, float inMax);

}  // namespace sw
