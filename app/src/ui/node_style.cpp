// ui/node_style — see node_style.h. Zone: ui. Pure color math; reads NodeSpec only.
#include "ui/node_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "runtime/graph.h"  // NodeSpec / PortSpec

namespace sw::ui {
namespace {

// TiXL base colors per dataType (Editor/Gui/Styling/UiColors.cs). Our dataTypes today:
// Float, Points, ParticleForce. Unknown → ColorForValues gray (TiXL default-ish).
ImVec4 baseColor(const std::string& dt) {
  if (dt == "Points")        return ImVec4(0.72f, 0.20f, 0.18f, 1.0f);   // ColorForGpuData (red)
  if (dt == "ParticleForce") return ImVec4(0.132f, 0.722f, 0.762f, 1.0f);// ColorForCommands (cyan)
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
