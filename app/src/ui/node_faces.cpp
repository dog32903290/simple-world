// ui/node_faces — see node_faces.h. Zone: ui (imgui draw). Depends on app(audio_monitor)
// + runtime(graph/spectrum). Never the reverse; never mutates the graph.
#include "ui/node_faces.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "imgui.h"

#include "app/audio_monitor.h"
#include "runtime/graph.h"
#include "runtime/spectrum_analyzer.h"

namespace sw::ui {

void drawAudioReactionFace(const sw::Node& node) {
  // TiXL-parity audio-reaction face — faithful port of TiXL AudioReactionUi.DrawChildUi.
  // The 32 FFT bands are drawn as bars COLORED by whether each band sits inside the
  // detection window (WindowCenter ± WindowWidth, with WindowEdge falloff): bars inside
  // the window glow, bars outside fade to gray — so 柏為 SEES the frequency range he is
  // listening to, and it moves live as he drags the Inspector sliders. A threshold line +
  // a level meter that flashes on a hit show the "got louder -> fired" moment.
  // Reads live data only (spectrum + node.params + outCache); the flash decay is the one
  // bit of view-local transient state (keyed by node id, never persisted).
  const sw::SpectrumSnapshot sp = sw::audio_monitor::spectrum();
  auto par = [&](const char* id, float def) {
    auto it = node.params.find(id);
    return it != node.params.end() ? it->second : def;
  };
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
  if (node.outCache[1] > 0.5f) s_lastHit[node.id] = now;
  float since = now - s_lastHit[node.id];
  float fl = 1.0f - since * 3.0f;
  fl = fl < 0.0f ? 0.0f : (fl > 1.0f ? 1.0f : fl);
  const float flash = fl * fl * fl * fl;
  const float mx = rmin.x + graphW + 6.0f;
  const float meterW = w - graphW - 8.0f;
  const float lvl = std::min(node.outCache[0], 1.0f);
  dl->AddRect(ImVec2(mx, rmin.y), ImVec2(mx + meterW, rmax.y), IM_COL32(90, 90, 100, 180));
  dl->AddRectFilled(ImVec2(mx + 1.0f, bottom - lvl * headroom), ImVec2(mx + meterW - 1.0f, bottom),
                    mix(cGray, cHi, flash));

  // AccumulatedLevel mode only: the meter value is unbounded (wraps at 10000), so the bar
  // just pegs and lies. TiXL's answer (and ours) is a spinner arc whose angle winds with the
  // accumulator — the one honest readout for this mode. Hidden in every other Output mode.
  if ((int)par("Output", 3.0f) == 4) {
    const float twoPi = 6.2831853f;
    float a = node.outCache[0];
    a -= (float)((long)(a / twoPi)) * twoPi;  // mod 2π without <cmath>
    const ImVec2 c(mx + meterW * 0.5f, rmin.y + (rmax.y - rmin.y) * 0.5f);
    dl->PathClear();
    dl->PathArcTo(c, headroom * 0.28f, a, a + 2.6f, 20);
    dl->PathStroke(cHi, ImDrawFlags_None, 3.0f);
  }

  char lbl[32];
  std::snprintf(lbl, sizeof(lbl), node.outCache[1] > 0.5f ? "HIT #%d" : "#%d",
                (int)node.outCache[2]);
  ImGui::TextDisabled("%s", lbl);
}

}  // namespace sw::ui
