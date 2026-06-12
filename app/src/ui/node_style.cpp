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
ImVec4 variation4(ImVec4 c, float bri, float sat, float op) {
  float h, s, v;
  ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, h, s, v);
  s = std::min(s * sat, 1.0f);
  v = std::min(v * bri, 1.0f);
  float r, g, b;
  ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
  return ImVec4(r, g, b, std::min(c.w * op, 1.0f));
}
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

// Idle-fade background (TiXL DrawNode.cs:121-127 Color.Mix + OperatorBackgroundIdle.Apply):
//   active   = OperatorBackground.Apply(typeColor)  = variation(b=0.5, s=0.7, op=1.0)
//   idle     = OperatorBackgroundIdle.Apply(typeColor) = variation(b=0.71, s=1.0, op=0.3)
//   idleFadeFactor = RemapAndClamp(framesSince, 0, 60, 1.0, 0.6)  (1.0=active, 0.6=idle)
//   idleFactor     = 1 - idleFadeFactor (0=active, 1=idle)
//   result = Mix(active, idle, idleFactor) = RGBA linear lerp
ImU32 nodeBgColorIdle(const sw::NodeSpec& spec, float idleFadeFactor) {
  const ImVec4 typeCol = baseColor(categoryType(spec));
  const ImVec4 active  = variation4(typeCol, 0.5f, 0.7f, 1.0f);  // OperatorBackground
  const ImVec4 idle    = variation4(typeCol, 0.71f, 1.0f, 0.3f); // OperatorBackgroundIdle
  // idleFadeFactor is [1.0 (active) → 0.6 (fully idle)]; idleFactor is [0→1]
  const float t = 1.0f - idleFadeFactor;  // t=0 -> active, t=1 -> idle
  // Clamp t to [0,1] for safety (idleFadeFactor is clamped at call site but guard here too)
  const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return packU32(ImVec4(
      active.x + (idle.x - active.x) * tc,
      active.y + (idle.y - active.y) * tc,
      active.z + (idle.z - active.z) * tc,
      active.w + (idle.w - active.w) * tc
  ));
}

ImU32 nodeSelectedBorderColor() { return IM_COL32(255, 255, 255, 255); }  // TiXL UiColors.Selection
ImU32 nodeHoverBorderColor()    { return IM_COL32(210, 210, 220, 170); }

// V2: TiXL DrawConnection.cs:32-42 — line color = ConnectionLines variation (b1,s1,op0.8)
// applied to the type's base color. DrawConnection.cs:40-44 applies TWO variations:
//   typeColor = ConnectionLines.Apply(selectedColor)
//   selectedColor = (selected||hovered) ? OperatorLabel.Apply(color) : ConnectionLines.Apply(color)
// so a normal line is ConnectionLines TWICE (alpha 0.8*0.8 = 0.64) and a hovered line is
// ConnectionLines over OperatorLabel. (refuter-R-VK V2: the single application shipped first
// read 20% too bright.)
ImU32 connectionLineColor(const std::string& dataType, bool selected) {
  ImVec4 base = baseColor(dataType);
  ImVec4 sel = selected ? variation4(base, 1.3f, 0.4f, 1.0f)   // OperatorLabel
                        : variation4(base, 1.0f, 1.0f, 0.8f);  // ConnectionLines (first pass)
  return variation(sel, 1.0f, 1.0f, 0.8f);                     // ConnectionLines (cs:44, on top)
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
  auto alpha = [](ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c).w; };

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
  // P4 (idle fade color): nodeBgColorIdle(1.0) == nodeBgColor (fully active = no fade),
  // nodeBgColorIdle(0.6) has lower alpha (OperatorBackgroundIdle op=0.3 lerp reduces alpha),
  // and the idle color differs from the active color (they must not be equal).
  bool idleFadeActive = false;
  bool idleColorDiffers = false;
  if (s) {
    bgDarkerThanLabel = maxc(nodeBgColor(*s)) < maxc(nodeLabelColor(*s));
    if (injectBug) bgDarkerThanLabel = !bgDarkerThanLabel;  // flip → must FAIL

    ImU32 active = nodeBgColorIdle(*s, 1.0f);  // idleFadeFactor=1.0 -> fully active
    ImU32 idle   = nodeBgColorIdle(*s, 0.6f);  // idleFadeFactor=0.6 -> fully idle
    ImU32 normal = nodeBgColor(*s);
    // Active (idleFadeFactor=1.0) should equal the plain nodeBgColor (t=0 → no idle mixing)
    idleFadeActive = (active == normal);
    // The idle variant must have lower alpha (OperatorBackgroundIdle op=0.3 lowers the mix alpha)
    idleColorDiffers = (alpha(idle) < alpha(active));
    if (injectBug) idleColorDiffers = !idleColorDiffers;  // flip → must FAIL
  }

  bool ok = grayFloat && redPoints && specFound && bgDarkerThanLabel && idleFadeActive && idleColorDiffers;
  std::printf("[selftest-nodestyle] grayFloat=%d redPoints=%d spec=%d bgDarkerThanLabel=%d "
              "idleFadeActive=%d idleColorDiffers=%d -> %s\n",
              grayFloat, redPoints, specFound, bgDarkerThanLabel,
              idleFadeActive, idleColorDiffers, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::ui
