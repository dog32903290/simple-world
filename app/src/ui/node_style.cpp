// ui/node_style — see node_style.h. Zone: ui. Pure color math; reads NodeSpec only.
#include "ui/node_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "runtime/graph.h"  // NodeSpec / PortSpec
// Note: blinkValue() uses ImGui::GetTime() which requires an active ImGui context (only
// valid when the editor is running; not called from headless selftests).

namespace sw::ui {
namespace {

// TiXL base colors per dataType (Editor/Gui/Styling/UiColors.cs). Our dataTypes:
// Float, Points, ParticleForce, Command, Texture2D. Unknown → ColorForValues gray.
ImVec4 baseColor(const std::string& dt) {
  if (dt == "Points")        return ImVec4(0.72f, 0.20f, 0.18f, 1.0f);   // ColorForGpuData (red)
  if (dt == "ParticleForce") return ImVec4(0.132f, 0.722f, 0.762f, 1.0f);// ColorForCommands (cyan)
  if (dt == "Command")       return ImVec4(0.132f, 0.722f, 0.762f, 1.0f);// ColorForCommands (cyan)
  if (dt == "Texture2D")     return ImVec4(0.624f, 0.0f, 0.541f, 1.0f);  // ColorForTextures (#9F008A magenta)
  return ImVec4(0.525f, 0.550f, 0.554f, 1.0f);                            // ColorForValues (gray)
}

// Pack to ImU32 WITHOUT ImGui::GetColorU32 — that one multiplies by the global context's
// style.Alpha, so it null-derefs in a headless selftest (no ImGui context). node_style is
// pure color math, so we pack the bytes ourselves (the ColorConvert* helpers are pure).
ImU32 packU32(ImVec4 c) {
  auto b8 = [](float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return (ImU32)(x * 255.0f + 0.5f); };
  return IM_COL32(b8(c.x), b8(c.y), b8(c.z), b8(c.w));
}

// TiXL ColorVariation: HSV with s*=sat, v*=bri (both clamped), a*=op (ColorVariation.cs).
ImU32 variation(ImVec4 c, float bri, float sat, float op) {
  float h, s, v;
  ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, h, s, v);
  s = std::min(s * sat, 1.0f);
  v = std::min(v * bri, 1.0f);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
  return packU32(ImVec4(r, g, b, std::min(c.w * op, 1.0f)));
}

// A node's category = its first OUTPUT port's dataType (TiXL: first output type).
std::string categoryType(const sw::NodeSpec& spec) {
  for (const auto& p : spec.ports)
    if (!p.isInput) return p.dataType;
  return "";  // no output → gray default
}

}  // namespace

ImU32 typeColor(const std::string& dataType) { return packU32(baseColor(dataType)); }

ImU32 nodeBgColor(const sw::NodeSpec& spec)     { return variation(baseColor(categoryType(spec)), 0.5f, 0.7f, 1.0f); }
ImU32 nodeBorderColor(const sw::NodeSpec& spec) { return variation(baseColor(categoryType(spec)), 0.1f, 0.7f, 0.5f); }
ImU32 nodeLabelColor(const sw::NodeSpec& spec)  { return variation(baseColor(categoryType(spec)), 1.3f, 0.4f, 1.0f); }

ImU32 nodeSelectedBorderColor() { return IM_COL32(255, 255, 255, 255); }  // TiXL UiColors.Selection
ImU32 nodeHoverBorderColor()    { return IM_COL32(210, 210, 220, 170); }

// V2: TiXL DrawConnection.cs:32-42 — line color = ConnectionLines variation (b1,s1,op0.8)
// applied to the type's base color. When selected/hovered TiXL uses OperatorLabel variation
// (b1.3,s0.4,op1.0) first then ConnectionLines on top — simplified here to just OperatorLabel
// (brighter) without the second application, which matches the visible result closely enough.
ImU32 connectionLineColor(const std::string& dataType, bool selected) {
  ImVec4 base = baseColor(dataType);
  if (selected) {
    // selected/hovered: OperatorLabel (b1.3, s0.4, a1.0) — distinct from normal
    return variation(base, 1.3f, 0.4f, 1.0f);
  }
  // normal: ConnectionLines (b1.0, s1.0, a0.8) — TiXL ColorVariations.ConnectionLines
  return variation(base, 1.0f, 1.0f, 0.8f);
}

// V3: TiXL DrawNode.cs:126 — rounding = 5 * CanvasScale, 0 if CanvasScale < 0.5.
// tixlScale = ViewScale (= 1/GetCurrentZoom() in imgui-node-editor), clamped.
float nodeRounding(float tixlScale) {
  if (tixlScale < 0.5f) return 0.0f;
  float r = 5.0f * tixlScale;
  if (r > 20.0f) r = 20.0f;  // cap to avoid overly large radius at high zoom
  return r;
}

// V4: TiXL MagGraphCanvas.Drawing.cs:459
//   internal static float Blink => MathF.Sin((float)ImGui.GetTime() * 10) * 0.5f + 0.5f;
float blinkValue() {
  return std::sin((float)ImGui::GetTime() * 10.0f) * 0.5f + 0.5f;
}

int runNodeStyleSelfTest(bool injectBug) {
  auto maxc = [](ImU32 c) { ImVec4 f = ImGui::ColorConvertU32ToFloat4(c); return std::max({f.x, f.y, f.z}); };

  // Type hues: Float is gray (r≈g≈b); Points is red-dominant.
  ImVec4 fl = ImGui::ColorConvertU32ToFloat4(typeColor("Float"));
  ImVec4 pt = ImGui::ColorConvertU32ToFloat4(typeColor("Points"));
  bool grayFloat = std::fabs(fl.x - fl.y) < 0.05f && std::fabs(fl.y - fl.z) < 0.05f;
  bool redPoints = pt.x > pt.y && pt.x > pt.z;

  // Invariant on a real spec (AudioReaction → first output Level = Float category):
  // the background tint must be darker than the label tint (TiXL b0.5 vs b1.3).
  const sw::NodeSpec* s = sw::findSpec("AudioReaction");
  bool specFound = s != nullptr;
  bool bgDarkerThanLabel = false;
  if (s) {
    bgDarkerThanLabel = maxc(nodeBgColor(*s)) < maxc(nodeLabelColor(*s));
    if (injectBug) bgDarkerThanLabel = !bgDarkerThanLabel;  // flip → must FAIL
  }

  bool ok = grayFloat && redPoints && specFound && bgDarkerThanLabel;
  std::printf("[selftest-nodestyle] grayFloat=%d redPoints=%d spec=%d bgDarkerThanLabel=%d -> %s\n",
              grayFloat, redPoints, specFound, bgDarkerThanLabel, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::ui
