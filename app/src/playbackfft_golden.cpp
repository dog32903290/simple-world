// playbackfft_golden — --selftest-playbackfft. Closed-form golden for the PlaybackFFT FloatList producer
// (TiXL Operators/Lib/numbers/floats/io/PlaybackFFT.cs). Feed a KNOWN spectrum snapshot via
// setCurrentSpectrum, build a PlaybackFFT node with a chosen InputBand, cook it on the flat FloatList
// path (PointGraph::cook → cookFloatListNode runs the leaf), read the host list back via
// debugCookedFloatList(), and assert it equals the snapshot array THE MODE SELECTS, element-for-element.
//
// EXPECTED VALUE (independent-of-impl, GOLDEN_STANDARD 三特徵 #1): the mode→array mapping + list length is
// hand-derived from PlaybackFFT.cs:21-43 —
//   InputBand 0 RawFft               → FftGainBuffer       (1024 bins)  — SpectrumSnapshot.fftGain
//   InputBand 1 NormalizedFft        → FftNormalizedBuffer (1024 bins)  — SpectrumSnapshot.fftNormalized
//   InputBand 2 FrequencyBands       → FrequencyBands      (32 bands)   — SpectrumSnapshot.bands
//   InputBand 3 FrequencyBandsPeaks  → FrequencyBandPeaks  (32 bands)   — SpectrumSnapshot.peaks
//   InputBand 4 FrequencyBandsAttacks→ FrequencyBandAttacks(32 bands)   — SpectrumSnapshot.attacks
// The expected values are the DISTINCT SENTINELS we stamp into the snapshot (each array a DIFFERENT
// non-trivial ramp), NOT sw's own output — so a mode that copies the WRONG array (e.g. peaks↔attacks
// swap, or bands when RawFft was asked) goes RED. This is the closed-form; no analyzer / hardware runs.
//
// DIVERGING-MIDDLE (三特徵 #2): every probe reads a MID element (index 500 of the 1024-bin arrays; index
// 17 of the 32-band arrays) carrying a non-zero, non-saturated sentinel — not the [0] end, not a flat 0.
// Swapping the mode→array pick, or truncating/reordering the copy, moves that mid element → RED.
//
// TEETH (三特徵 #3): injectBug routes through floatListInjectBug() (drops the producer's LAST element in
// the REAL cook) → the length assertion + the last-element read go RED on the actual cook path, NOT by
// flipping the expected value. did-not-trip → return 0 (the --bite NO-BITE list catches a dead tooth).
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/pinId
#include "runtime/playback_fft.h"            // setCurrentSpectrum
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedFloatList
#include "runtime/spectrum_analyzer.h"       // SpectrumSnapshot, kFftBins, kBandCount

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Stamp each snapshot array with a DISTINCT non-trivial ramp so a wrong-array copy is detectable at any
// index. The five arrays get five different base offsets + slopes → no two arrays share a value at the
// same index (mode confusion → RED). Values kept in (0,1) mid-range (analyzer arrays are 0..1 / rate).
SpectrumSnapshot makeSnapshot() {
  SpectrumSnapshot s{};
  for (int i = 0; i < kFftBins; ++i) {
    s.fftGain[i]       = 0.10f + 0.0003f * (float)i;  // ramp A  (RawFft)
    s.fftNormalized[i] = 0.20f + 0.0004f * (float)i;  // ramp B  (NormalizedFft)  — distinct from A everywhere
  }
  for (int i = 0; i < kBandCount; ++i) {
    s.bands[i]   = 0.30f + 0.011f * (float)i;  // ramp C  (FrequencyBands)
    s.peaks[i]   = 0.40f + 0.012f * (float)i;  // ramp D  (FrequencyBandPeaks)
    s.attacks[i] = 0.50f + 0.013f * (float)i;  // ramp E  (FrequencyBandAttacks)  — distinct from C,D
  }
  return s;
}

// Cook PlaybackFFT(InputBand=mode) on the flat path against the CURRENT snapshot; return the host list.
std::vector<float> cookPlaybackFFT(PointGraph& pg, int mode) {
  Graph g;
  Node n; n.id = 1; n.type = "PlaybackFFT";
  n.params["InputBand"] = (float)mode;   // single scalar Float param (resolved, not gathered)
  g.nodes.push_back(n);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::vector<float>* out = pg.debugCookedFloatList(1);
  return out ? *out : std::vector<float>{};
}

// The expected array for a mode, straight off the snapshot fields (the hand-derived oracle).
std::vector<float> expectedFor(const SpectrumSnapshot& s, int mode) {
  switch (mode) {
    case 0: return {s.fftGain.begin(), s.fftGain.end()};
    case 1: return {s.fftNormalized.begin(), s.fftNormalized.end()};
    case 2: return {s.bands.begin(), s.bands.end()};
    case 3: return {s.peaks.begin(), s.peaks.end()};
    case 4: return {s.attacks.begin(), s.attacks.end()};
    default: return {};
  }
}

// Compare a full list to the expected snapshot array (length + every element). A mid-element mismatch or a
// wrong length fails. Prints the first divergence.
bool listMatches(const char* tag, const std::vector<float>& got, const std::vector<float>& want) {
  if (got.size() != want.size()) {
    std::printf("[selftest-playbackfft] %s length %zu != %zu\n", tag, got.size(), want.size());
    return false;
  }
  for (size_t i = 0; i < want.size(); ++i) {
    if (!nearf(got[i], want[i])) {
      std::printf("[selftest-playbackfft] %s [%zu] %.4f != %.4f\n", tag, i, got[i], want[i]);
      return false;
    }
  }
  return true;
}

}  // namespace

int runPlaybackFFTSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  const SpectrumSnapshot snap = makeSnapshot();
  setCurrentSpectrum(snap);

  floatListInjectBug() = injectBug;
  bool ok = true;
  bool deadTooth = false;   // set if injectBug did NOT trip (→ return 0 for the --bite NO-BITE list)

  // Names for readable output; mid-index probes (mid of 1024 / mid of 32) checked explicitly below.
  static const char* kModeName[5] = {"RawFft", "NormalizedFft", "FrequencyBands",
                                     "FrequencyBandsPeaks", "FrequencyBandsAttacks"};

  for (int mode = 0; mode < 5; ++mode) {
    std::vector<float> got  = cookPlaybackFFT(pg, mode);
    std::vector<float> want = expectedFor(snap, mode);

    if (injectBug) {
      // TEETH: the real cook dropped the last element → the length must diverge from the oracle. If the
      // list is empty the drop can't bite (defensive; our arrays are non-empty) → treat as not-tripped.
      if (want.empty()) continue;
      if (got.size() == want.size()) {   // did-not-trip
        std::printf("[selftest-playbackfft] BUG %s did-not-trip (len %zu==%zu)\n",
                    kModeName[mode], got.size(), want.size());
        deadTooth = true;
        break;
      }
      continue;  // this mode bit; check the rest
    }

    // GREEN: full-list equality + an explicit mid-element probe (diverging-middle, non-saturated).
    bool full = listMatches(kModeName[mode], got, want);
    size_t mid = want.size() / 2;  // 512 for 1024-bin, 16 for 32-band — a mid, non-endpoint index
    bool midOk = (mid < got.size() && mid < want.size()) ? nearf(got[mid], want[mid]) : false;
    if (!midOk)
      std::printf("[selftest-playbackfft] %s MID [%zu] mismatch\n", kModeName[mode], mid);
    ok = ok && full && midOk;
    if (mode == 0)  // one representative echo
      std::printf("[selftest-playbackfft] %s len=%zu mid[%zu]=%.4f (want %.4f)\n",
                  kModeName[mode], got.size(), mid, got.empty() ? 0.f : got[mid],
                  mid < want.size() ? want[mid] : 0.f);
  }

  floatListInjectBug() = false;  // restore

  int rc;
  if (injectBug) {
    // --bite contract (run_all_selftests.sh): a -bug variant must exit NON-zero when the tooth BITES.
    // did-not-trip → return 0 so the NO-BITE list catches a dead tooth (never a false exit 1).
    if (deadTooth) rc = 0;
    else { std::printf("[selftest-playbackfft] BUG bit (dropped element) — RED as required\n"); rc = 1; }
  } else {
    std::printf("[selftest-playbackfft] %s\n", ok ? "PASS" : "FAIL");
    rc = ok ? 0 : 1;
  }
  pool->release();
  return rc;
}

}  // namespace sw
