// platform/wled_frame — Adalight serial frame assembly for WLedSerialOutput (pure, closed-form).
//
// The BODY of WLedSerialOutput.cs is not a socket op — it's a deterministic byte-buffer builder:
// header + per-LED color mapping (Stretch/Repeat/Lerp) + color-order permutation + brightness scale.
// That closed-form logic is the golden meat; the serial WRITE is the same thin platform seam as any
// other serial device. Exposing it as a pure function keeps the golden independent of the transport.
//
// Ground truth (external/tixl, read-only): Operators/Lib/io/serial/WLedSerialOutput.cs
//   :243-262  EnsureBuffer — Adalight header 'A','d','a', countHi, countLo, checksum(hi^lo^0x55).
//   :146-155  color-order → rPos/gPos/bPos permutation table.
//   :164-211  per-LED map: Repeat (i%src), Lerp (linear interp), Stretch (nearest i*src/led).
//   :264-271  ToByte — clamp [0,1], round v*255+0.5.
#pragma once
#include <cstdint>
#include <vector>

namespace sw {

// Mirrors WLedSerialOutput.MapModes / ColorOrders (same integer order as the C# enums).
enum class WledMapMode : int { Stretch = 0, Repeat = 1, Lerp = 2 };
enum class WledColorOrder : int { GRB = 0, RGB = 1, BRG = 2, RBG = 3, BGR = 4, GBR = 5 };

// One source color, linear [0,1] per channel (mirrors Vector4; alpha ignored — WLED uses X/Y/Z).
struct WledColor { float r, g, b; };

// Build the exact Adalight serial frame WLedSerialOutput writes. `colors` is the source list; the
// output has `ledCount` LEDs (clamped to [1, 4096] per MaxLedCount; ledCount<=0 → colors.size()).
// Byte-for-byte identical to WLedSerialOutput.UpdateInner's _frameBuffer.
//
// Frame = [ 'A' 'd' 'a', (ledCount-1)>>8, (ledCount-1)&0xFF, checksum ] + ledCount * 3 color bytes.
std::vector<uint8_t> buildWledFrame(const std::vector<WledColor>& colors, int ledCount,
                                    WledMapMode mapMode, WledColorOrder colorOrder, float brightness);

// The ToByte quantization exposed for direct assertion (clamp [0,1], round v*255+0.5). WLED :264.
uint8_t wledToByte(float v);

}  // namespace sw
