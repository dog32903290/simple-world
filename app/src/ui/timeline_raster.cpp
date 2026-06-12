// ui/timeline_raster — zoom-adaptive bar/beat/tick ruler ladder for the S6 timeline.
// = TiXL BeatTimeRaster (BeatTimeRaster.cs) + AbstractTimeRaster.DrawTimeTicks/TryGetRastersForScale
// (AbstractTimeRaster.cs), ported verbatim:
//   - levels (bars): tick 1/16, beat 1/4, bar 1, measure 4, phrase 16, 10-phrases 160
//   - range pick: first ScaleRange whose ScaleMax >= invertedScale (= 1/pxPerBar). TiXL's
//     TimeRasterDensity knob cancels out for BeatTimeRaster (ranges pre-divide by density and the
//     comparison divides again) so the table below uses the raw constants.
//   - fadeFactor = 1 - remapClamp(invertedScale, ScaleMin, ScaleMax): the finest level fades out
//     as you zoom away, then the ladder switches gear.
//   - labels: 'b' -> bar int, '.' -> ".beat" (bars*4 % 4), ':' -> ":tick" (bars*16 % 4)
//   - px dedupe across levels, first listed raster wins the pixel (= _usedPositions)
// Pure data-out (no imgui): the window renders the ticks; collectSnapAnchors reuses them as the
// raster's snap attractor (= AbstractTimeRaster.CheckForSnap on _usedPositions).
// Zone: ui (timeline-internal).
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <vector>

#include "ui/timeline_internal.h"

namespace sw::ui::tl {
namespace {

constexpr double kTick = 1.0 / 16.0;   // = everyTick   (1/240*60/4 bars)
constexpr double kBeat = 0.25;         // = everyBeat   (1/240*60)
constexpr double kBar = 1.0;           // = everyBar    (4/240*60)
constexpr double kMeasure = 4.0;       // = everyMeasure
constexpr double kPhrase = 16.0;       // = everyPhrase
constexpr double kTenPhrases = 160.0;  // = every10Phrases

// fmt: 'b' bar label, '.' bar.beat label, ':' tick label, 0 = line only.
struct RasterDef { char fmt; double spacing; bool fadeLabels; bool fadeLines; };
struct ScaleRange { double scaleMin, scaleMax; RasterDef rasters[3]; int count; };

// = BeatTimeRaster.InitScaleRanges, row for row (density cancelled out, see header).
const ScaleRange kRanges[] = {
    {0.0, 0.001, {{'b', kBar, false, false}, {'.', kBeat, false, false}, {':', kTick, true, false}}, 3},
    {0.001, 0.002, {{'b', kBar, false, false}, {'.', kBeat, false, false}, {0, kTick, false, true}}, 3},
    {0.002, 0.005, {{'b', kBar, false, false}, {'.', kBeat, true, false}}, 2},
    {0.005, 0.007, {{'b', kBar, false, false}, {0, kBeat, true, false}}, 2},
    {0.007, 0.01, {{'b', kBar, false, false}, {0, kBeat, true, true}}, 2},
    {0.01, 0.012, {{'b', kBar, false, false}}, 1},
    {0.012, 0.03, {{'b', kMeasure, false, false}, {'b', kBar, true, false}}, 2},
    {0.03, 0.06, {{'b', kMeasure, false, false}, {0, kBar, true, true}}, 2},
    {0.06, 0.1, {{'b', kMeasure, false, false}}, 1},
    {0.1, 0.15, {{'b', kPhrase, false, false}, {'b', kMeasure, true, false}}, 2},
    {0.15, 0.3, {{'b', kPhrase, false, false}, {0, kMeasure, true, true}}, 2},
    {0.3, 0.5, {{'b', kTenPhrases, false, false}, {'b', kPhrase, true, false}}, 2},
    {0.5, 1.2, {{'b', kTenPhrases, false, false}, {0, kPhrase, true, true}}, 2},
    {1.2, 2.0, {{'b', kTenPhrases, true, false}}, 1},
    {2.0, 999.0, {{0, kTenPhrases, false, false}}, 1},
};

// = BeatTimeRaster.BuildLabel: 'b' -> bars, '.' -> ".<beat 0-3>", ':' -> ":<tick 0-3>".
void buildLabel(char fmt, double bars, char* out, size_t n) {
  switch (fmt) {
    case 'b': snprintf(out, n, "%d", (int)bars); break;
    case '.': snprintf(out, n, "%d.%d", (int)bars, (int)(bars * 4.0) % 4); break;
    case ':': snprintf(out, n, ":%d", (int)(bars * 16.0) % 4); break;
    default: out[0] = '\0'; break;
  }
}

}  // namespace

void computeRaster(double pxPerBar, double scrollBars, double widthPx, std::vector<RasterTick>& out) {
  out.clear();
  if (!(pxPerBar > 1e-5) || !(widthPx > 0.0) || !std::isfinite(scrollBars)) return;
  const double inv = 1.0 / pxPerBar;  // = invertedScale (bars per px)

  // TryGetRastersForScale: first range whose ScaleMax >= invertedScale.
  const ScaleRange* range = nullptr;
  for (const ScaleRange& r : kRanges) {
    if (r.scaleMax < inv) continue;
    range = &r;
    break;
  }
  if (!range) return;
  const double remap = (inv - range->scaleMin) / (range->scaleMax - range->scaleMin);
  const float fade = 1.0f - (float)std::clamp(remap, 0.0, 1.0);

  std::unordered_set<int> used;  // = _usedPositions px dedupe (first listed raster wins)
  for (int ri = 0; ri < range->count; ++ri) {
    const RasterDef& rd = range->rasters[ri];
    const float lineAlpha = rd.fadeLines ? fade : 1.0f;
    const float labelAlpha = rd.fadeLabels ? fade : 1.0f;
    // = DrawTimeTicks: t starts at -scroll % spacing, steps by spacing while on screen.
    double t = std::fmod(-scrollBars, rd.spacing);
    int guard = 0;
    while (t * pxPerBar < widthPx && ++guard < 100000) {
      const int xi = (int)(t * pxPerBar);
      if (xi > 0 && xi < (int)widthPx && used.find(xi) == used.end()) {
        used.insert(xi);
        RasterTick tick;
        tick.bars = t + scrollBars;
        tick.lineAlpha = lineAlpha;
        tick.labelAlpha = labelAlpha;
        buildLabel(rd.fmt, tick.bars, tick.label, sizeof(tick.label));
        out.push_back(tick);
      }
      t += rd.spacing;
    }
  }
}

}  // namespace sw::ui::tl
