// ui/node_faces — see node_faces.h. Zone: ui (imgui draw). Depends on app(audio_monitor/
// document/frame_cook) + runtime(compound_graph/spectrum). Never the reverse; never mutates
// the lib.
#include "ui/node_faces.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "imgui.h"

#include "app/audio_monitor.h"
#include "app/document.h"
#include "app/frame_cook.h"  // residentOut: live extOut values by resident path
#include "runtime/compound_graph.h"
#include "runtime/graph.h"   // findSpec — read a child's first-output dataType (Texture2D thumbnail gate)
#include "runtime/spectrum_analyzer.h"
#include "ui/node_style.h"   // typeColor — the Texture2D type color for the thumbnail border (TiXL)
#include "verify/eye/eye.h"  // recordRect — ONE hook so the harness can assert thumbnail presence

// Metal type used only as an opaque ImTextureID handle here (NOT a Metal call: ui stays Metal-free,
// the shell owns the texture). Forward-declared so node_faces.h needs no Metal include.
namespace MTL { class Texture; }

namespace sw {
// Shell seam (main.cpp): the texture the resident node at `path` cooked this frame, or nullptr. Same
// opaque-handle contract as previewTexture() — ui asks the shell, never touches Metal itself.
MTL::Texture* residentNodeTexture(const char* path);
// Companion: native pixel size of that texture (for the thumbnail aspect), so ui never queries Metal.
// false (w/h untouched) when the path has no cooked texture this frame.
bool residentNodeTextureSize(const char* path, int& w, int& h);
}

namespace sw::ui {
namespace {  // face fns are internal — reached only via the kFaces dispatch table below

void drawAudioReactionFace(const sw::SymbolChild& child) {
  // TiXL-parity audio-reaction face — faithful port of TiXL AudioReactionUi.DrawChildUi.
  // The 32 FFT bands are drawn as bars COLORED by whether each band sits inside the
  // detection window (WindowCenter ± WindowWidth, with WindowEdge falloff): bars inside
  // the window glow, bars outside fade to gray — so 柏為 SEES the frequency range he is
  // listening to, and it moves live as he drags the Inspector sliders. A threshold line +
  // a level meter that flashes on a hit show the "got louder -> fired" moment.
  // Reads live data only (spectrum + effective params + resident extOut); the flash decay
  // is the one bit of view-local transient state (keyed by child id, never persisted).
  const sw::SpectrumSnapshot sp = sw::audio_monitor::spectrum();
  auto par = [&](const char* id, float def) {
    return sw::effectiveInput(sw::doc::g_lib(), child, id, def);
  };
  // Live outputs (level / wasHit / hitCount) off the resident node this child projects to.
  static const float kZero[3] = {0.0f, 0.0f, 0.0f};
  const float* out = sw::framecook::residentOut(sw::doc::residentPathFor(child.id).c_str());
  if (!out) out = kZero;
  const float windowCenter = par("WindowCenter", 0.0f);
  const float windowWidth  = par("WindowWidth", 1.0f);
  const float windowEdge   = std::max(par("WindowEdge", 1.0f), 1e-4f);
  const float threshold    = std::min(par("Threshold", 0.5f), 1.0f);

  const ImU32 cHi   = IM_COL32(255, 170, 70, 255);   // StatusAnimated-ish highlight
  const ImU32 cGray = IM_COL32(120, 120, 130, 110);  // inactive band
  const ImU32 cLine = IM_COL32(235, 235, 240, 200);
  auto mix = [](ImU32 a, ImU32 b, float t) -> ImU32 {  // lerp a->b (no imgui_internal)
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::GetColorU32(ImVec4(ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t,
                                     ca.z + (cb.z - ca.z) * t, ca.w + (cb.w - ca.w) * t));
  };

  const ImVec2 sz(190.0f, 64.0f);
  ImGui::Dummy(sz);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 rmin = ImGui::GetItemRectMin();
  const ImVec2 rmax = ImGui::GetItemRectMax();
  const float w = sz.x, headroom = sz.y - 4.0f;
  const float bottom = rmax.y - 1.0f;
  const float graphW = w * 0.74f;
  dl->AddRectFilled(rmin, rmax, IM_COL32(20, 20, 24, 255));

  // FFT bars, colored by distance from the detection window (factor 0=inside, 1=outside).
  const int bars = (int)sw::kBandCount;
  const float barW = graphW / bars;
  for (int i = 0; i < bars; ++i) {
    const float v = std::min(std::max(sp.bands[i], 0.0f), 1.0f);
    const float f = bars > 1 ? (float)i / (bars - 1) : 0.0f;
    float d = f - windowCenter;
    if (d < 0.0f) d = -d;
    const float factor = d / windowEdge - windowWidth / windowEdge;
    const float x = rmin.x + i * barW;
    dl->AddRectFilled(ImVec2(x, bottom - v * headroom), ImVec2(x + barW - 1.0f, bottom),
                      mix(cHi, cGray, factor));
  }

  // Threshold line across the spectrum + window bracket along the bottom axis.
  const float thY = bottom - threshold * headroom;
  dl->AddLine(ImVec2(rmin.x, thY), ImVec2(rmin.x + graphW, thY), cLine, 1.0f);
  const float wx1 = rmin.x + std::max(windowCenter - windowWidth * 0.5f, 0.0f) * graphW;
  const float wx2 = rmin.x + std::min(windowCenter + windowWidth * 0.5f, 1.0f) * graphW;
  dl->AddRectFilled(ImVec2(wx1, rmax.y - 3.0f), ImVec2(wx2, rmax.y - 1.0f), cHi);

  // Level meter on the right: bar HEIGHT = current Level (rises as it gets louder),
  // bar COLOR flashes orange on a hit. Flash decays from the WasHit edge (view-local).
  static std::map<int, float> s_lastHit;
  const float now = (float)ImGui::GetTime();
  if (out[1] > 0.5f) s_lastHit[child.id] = now;
  float since = now - s_lastHit[child.id];
  float fl = 1.0f - since * 3.0f;
  fl = fl < 0.0f ? 0.0f : (fl > 1.0f ? 1.0f : fl);
  const float flash = fl * fl * fl * fl;
  const float mx = rmin.x + graphW + 6.0f;
  const float meterW = w - graphW - 8.0f;
  const float lvl = std::min(out[0], 1.0f);
  dl->AddRect(ImVec2(mx, rmin.y), ImVec2(mx + meterW, rmax.y), IM_COL32(90, 90, 100, 180));
  dl->AddRectFilled(ImVec2(mx + 1.0f, bottom - lvl * headroom), ImVec2(mx + meterW - 1.0f, bottom),
                    mix(cGray, cHi, flash));

  // AccumulatedLevel mode only: the meter value is unbounded (wraps at 10000), so the bar
  // just pegs and lies. TiXL's answer (and ours) is a spinner arc whose angle winds with the
  // accumulator — the one honest readout for this mode. Hidden in every other Output mode.
  if ((int)par("Output", 3.0f) == 4) {
    const float twoPi = 6.2831853f;
    float a = out[0];
    a -= (float)((long)(a / twoPi)) * twoPi;  // mod 2π without <cmath>
    const ImVec2 c(mx + meterW * 0.5f, rmin.y + (rmax.y - rmin.y) * 0.5f);
    dl->PathClear();
    dl->PathArcTo(c, headroom * 0.28f, a, a + 2.6f, 20);
    dl->PathStroke(cHi, ImDrawFlags_None, 3.0f);
  }

  char lbl[32];
  std::snprintf(lbl, sizeof(lbl), out[1] > 0.5f ? "HIT #%d" : "#%d", (int)out[2]);
  ImGui::TextDisabled("%s", lbl);
}

// TiXL parity (MagGraphCanvas.TryDrawTexturePreview): a node whose FIRST output is a Texture2D draws
// a small thumbnail of its rendered output on its body — so 柏為 SEES the image each node makes, on the
// canvas, without opening the Output window. Returns true if a thumbnail was drawn (gate: first output
// dataType == "Texture2D" AND the node cooked a texture this frame — only nodes on the currently-viewed
// target chain cook one). Non-image nodes (Float/Points/Command/...) return false → no face, exactly
// like TiXL where only a Slot<Texture2D> output gets the preview.
//
// Fork vs TiXL (named): TiXL sizes the preview to the imgui-node-editor cell (GridSize) and right-aligns
// it inside the body with a per-canvas CanvasScale. Our canvas is imgui-node-editor (no MagGraph grid),
// so the face lays out like the AudioReaction face — an ImGui::Dummy reserving a fixed-height thumbnail
// inside the node body, width = height × texture-aspect (capped at maxAspect 1.6, TiXL's cap). Aspect +
// type-colored border + the "draw the live cooked texture" behaviour are faithful.
bool drawTexturePreviewFace(const sw::SymbolChild& child) {
  const sw::NodeSpec* spec = sw::findSpec(child.symbolId);
  if (!spec) return false;
  const sw::PortSpec* firstOut = nullptr;
  for (const sw::PortSpec& p : spec->ports)
    if (!p.isInput) { firstOut = &p; break; }
  if (!firstOut || firstOut->dataType != "Texture2D") return false;  // TiXL: firstOutput is Slot<Texture2D>

  MTL::Texture* tex = sw::residentNodeTexture(sw::doc::residentPathFor(child.id).c_str());
  if (!tex) return false;  // not cooked this frame (off the viewed target chain) → no thumbnail, no face

  // Aspect from the cooked texture; the seam exposes size via the existing previewTextureSize path, but
  // for a per-node tex we read width/height through the same opaque handle the shell hands us. We avoid a
  // Metal call in ui by routing size through residentNodeTextureSize (declared with residentNodeTexture).
  int tw = 0, th = 0;
  sw::residentNodeTextureSize(sw::doc::residentPathFor(child.id).c_str(), tw, th);
  const float aspect = (tw > 0 && th > 0) ? (float)tw / (float)th : 1.0f;

  const float kH = 38.0f;            // thumbnail height (px) on the body — fixed (canvas-scale fork above)
  const float kMaxAspect = 1.6f;     // TiXL maxAspect cap
  float w = kH * aspect, h = kH;
  if (w > kH * kMaxAspect) { w = kH * kMaxAspect; }  // TiXL clamps width (keeps a sane footprint)

  const ImVec2 sz(w, h);
  ImGui::Dummy(sz);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 rmin = ImGui::GetItemRectMin();
  const ImVec2 rmax = ImGui::GetItemRectMax();
  dl->AddImage(reinterpret_cast<ImTextureID>(tex), rmin, rmax);  // the live cooked output (TiXL AddImage)
  dl->AddRect(rmin, rmax, sw::ui::typeColor("Texture2D"), 2.0f, ImDrawFlags_RoundCornersAll, 1.0f);
  sw::eye::recordRect(("thumb:" + std::to_string(child.id)).c_str(), rmin.x, rmin.y, rmax.x, rmax.y);
  return true;
}

// 資料驅動 dispatch: add a {type, faceFn} row here to give an operator a custom body.
// New node types with a custom draw-list face append to this table — they never touch
// editor_ui's canvas loop, so node-making and canvas-skinning stay collision-free.
struct FaceEntry { const char* type; void (*draw)(const sw::SymbolChild&); };
const FaceEntry kFaces[] = {
    {"AudioReaction", drawAudioReactionFace},
};

}  // anonymous namespace

void drawNodeFace(const sw::SymbolChild& child) {
  // 1) explicit per-type custom face (kFaces table). 2) generic Texture2D-output thumbnail (TiXL
  // TryDrawTexturePreview) for every image node — data-driven by the node's first-output dataType,
  // no per-type row. An explicit face wins (a custom face draws its own body); the thumbnail is the
  // universal fallback for image ops that have no custom face.
  for (const auto& f : kFaces)
    if (child.symbolId == f.type) { f.draw(child); return; }
  drawTexturePreviewFace(child);
}

}  // namespace sw::ui
