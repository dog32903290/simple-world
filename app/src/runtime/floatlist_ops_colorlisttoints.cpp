// ColorListToInts floatlist op (COLORLIST→FLOATLIST BRIDGE consumer — the vec4-list rail feeds the
// int-list rail). Reads a ColorList MultiInput (each element a Vector4 = R,G,B,A in 0..1) and emits a
// List<int> (each channel scaled to 0..255) dissolved onto sw's FloatList/IntList currency (Cut32: sw
// has no Int port, ints ride the FloatList rail as integer-valued floats). Named floatlist_ops_*.cpp so
// the CMake glob (SW_FLOATLIST_OP_SRCS) picks it up; the OUTPUT is FloatList so it belongs on this rail.
//
// TiXL authority: external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.cs (ported verbatim):
//   Update() (cs:16-38):
//     list = Result.Value (cleared, cs:24);
//     _outputMode = OutputMode enum (cs:21);
//     foreach inputSlot in ColorLists.GetCollectedTypedInputs()  (cs:23,26 — connected inputs only):
//         l = inputSlot.GetValue(context);
//         if (l == null || l.Count == 0) continue;               (cs:29-30)
//         foreach c in l: AppendChannelValues(c);                (cs:32-35)
//   AppendChannelValues(Vector4 c) (cs:41-69) — per mode:
//     RGBA → X,Y,Z,W ;  ARGB → W,X,Y,Z ;  RGB → X,Y,Z ;  R → X ;  A → W
//   AppendAsInt(float f) (cs:71-74):  Result.Add( (int)(f * 255).Clamp(0,255) );
//     ★C# operator precedence: `.Clamp` binds to the float (f*255), so the value is CLAMPED IN FLOAT to
//      [0,255] and THEN cast (int) — a truncation toward zero. So the element = (int)clampf(f*255, 0, 255).
//   Modes enum (cs:86-93): RGBA=0, ARGB=1, RGB=2, R=3, A=4.  DEFAULT _outputMode = RGB (cs:76).
//
// EVAL-SIDE LAYOUT: a FloatList PRODUCER that consumes a ColorList MultiInput. ColorLists is gathered off
// the ColorList rail into ctx.inputColorLists (the bridge the FloatList cook driver added — flat +
// resident); OutputMode is an Int param dissolved to Float (value spine, fork-int-dissolve-to-float).
// This is the FIRST op to read inputColorLists (every prior FloatList op ignores it → byte-identical).
//
// FORKS (named):
//   - fork-int-dissolve-to-float: sw has NO Int port (Cut32). Each converted int is stored as an
//     integer-valued float in the FloatList output. Faithful to List<int> element semantics; a downstream
//     Int consumer reads the same integer.
//   - fork-clamp-in-float-then-truncate: matches C# `(int)(f*255).Clamp(0,255)` precedence exactly
//     (clamp the float, then truncate toward zero). E.g. f=1.0 → clampf(255,0,255)=255 → 255;
//     f=0.5 → 127.5 → 127 (truncate); f=-0.1 → clampf(-25.5,0,255)=0 → 0; f=2.0 → 255.
//   - fork-colorlisttoints-empty-when-unwired: GetCollectedTypedInputs() yields CONNECTED inputs only —
//     an unwired ColorList MultiInput → no gathered lists → empty output. A wired-but-empty list is
//     skipped (cs:29-30). Both handled by iterating ctx.inputColorLists (driver gathers connected wires).
#include <cmath>  // std::floor (truncate toward zero via (int) cast on a clamped non-negative float)

#include <simd/simd.h>

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListInjectBug
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// AppendAsInt (ColorListToInts.cs:71-74): (int)clampf(f*255, 0, 255) — clamp in float, then truncate.
// Stored as an integer-valued float (the dissolved IntList element).
float channelToInt(float f) {
  float scaled = f * 255.0f;
  if (scaled < 0.0f) scaled = 0.0f;
  if (scaled > 255.0f) scaled = 255.0f;
  return (float)(int)scaled;  // (int) truncates toward zero; scaled is in [0,255] → nearest-below integer
}

// AppendChannelValues (ColorListToInts.cs:41-69): push the channels for `c` in the mode's order.
void appendChannels(std::vector<float>& out, const simd::float4& c, int mode) {
  switch (mode) {
    case 0:  // RGBA
      out.push_back(channelToInt(c.x)); out.push_back(channelToInt(c.y));
      out.push_back(channelToInt(c.z)); out.push_back(channelToInt(c.w)); break;
    case 1:  // ARGB
      out.push_back(channelToInt(c.w)); out.push_back(channelToInt(c.x));
      out.push_back(channelToInt(c.y)); out.push_back(channelToInt(c.z)); break;
    case 2:  // RGB (default)
      out.push_back(channelToInt(c.x)); out.push_back(channelToInt(c.y));
      out.push_back(channelToInt(c.z)); break;
    case 3:  // R
      out.push_back(channelToInt(c.x)); break;
    case 4:  // A
      out.push_back(channelToInt(c.w)); break;
    default: break;  // no channels (matches no matching case in the C# switch)
  }
}

// ColorListToInts: clear output, then for each gathered ColorList (connected inputs, wire order) append
// each color's channels per OutputMode.
void cookColorListToInts(FloatListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();  // ColorListToInts.cs:24 — list.Clear()

  const int mode = (int)floatListParam(c.params, "OutputMode", 2.0f);  // default RGB (cs:76)

  if (c.inputColorLists) {
    for (const std::vector<simd::float4>& l : *c.inputColorLists) {
      if (l.empty()) continue;  // cs:29-30 — null/empty list skipped
      for (const simd::float4& color : l)
        appendChannels(*c.output, color, mode);
    }
  }

  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's
  // RED case bites here, NOT by flipping the expected value. Off in production.
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static FloatListOp — independent leaf .cpp (no shared edit point).
//   Ports: "ColorLists" = ColorList MultiInput (the vec4-list sources; driver gathers into inputColorLists);
//          "OutputMode" = Float/Int enum (0=RGBA 1=ARGB 2=RGB 3=R 4=A; default RGB); pinless param;
//          "out"        = the FloatList output (the dissolved List<int>).
static const FloatListOp _reg_colorlisttoints{
    {"ColorListToInts", "ColorListToInts",
     {{"ColorLists", "ColorLists", "ColorList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"OutputMode", "OutputMode", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
       {"RGBA", "ARGB", "RGB", "R", "A"}, /*pinless=*/true},
      {"out", "out", "FloatList", false}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookColorListToInts};

}  // namespace sw
