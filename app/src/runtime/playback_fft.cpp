// runtime/playback_fft — the PlaybackFFT node: a FloatList PRODUCER that publishes the live spectrum
// analysis (one of five arrays, selected by InputBand) onto the host FloatList rail so downstream ops
// (ValuesToTexture / drive-points / geometry) consume the FFT.
//
// TiXL AUTHORITY (verbatim below): Operators/Lib/numbers/floats/io/PlaybackFFT.cs
//   Update():
//     var mode = (AudioReaction.InputModes)InputBand.GetValue(context).Clamp(0, len-1);   // PlaybackFFT.cs:21
//     bins = mode switch {                                                                  // cs:22-41
//       RawFft               => AudioAnalysis.FftGainBuffer.ToList(),
//       NormalizedFft        => AudioAnalysis.FftNormalizedBuffer.ToList(),
//       FrequencyBands       => AudioAnalysis.FrequencyBands.ToList(),
//       FrequencyBandsPeaks  => AudioAnalysis.FrequencyBandPeaks.ToList(),
//       FrequencyBandsAttacks=> AudioAnalysis.FrequencyBandAttacks.ToList(),
//       _ => bins };                                                                        // (default: null)
//     Result.Value = bins;                                                                  // cs:43
//   A null source (analyzer buffer absent) yields EmptyList (cs:25/28/31/34/37). InputBand default = 0
//   (RawFft) per PlaybackFFT.t3:6 "DefaultValue": 0.
//
// AudioReaction.InputModes (io/audio/AudioReaction.cs:185-192) — the SAME enum PlaybackFFT clamps against:
//   0 RawFft, 1 NormalizedFft, 2 FrequencyBands, 3 FrequencyBandsPeaks, 4 FrequencyBandsAttacks.
//   sw maps these 1:1 onto SpectrumSnapshot fields (spectrum_analyzer.h): fftGain / fftNormalized /
//   bands / peaks / attacks. TiXL's raw arrays are 1024 FFT bins (RawFft/NormalizedFft) or 32 bands.
//
// DATA SEAM (leaf discipline, ARCHITECTURE.md "葉子接縫"): the analyzer is owned by the app (audio_monitor
// via platform capture). This runtime leaf does NOT include the analyzer or hardware — it holds a
// process-static CURRENT snapshot that the APP publishes once per frame (setCurrentSpectrum), the exact
// mirror of how frame_cook hands cookAudioReaction the live `spec`. PlaybackFFT is PULL-driven off the
// FloatList rail (a downstream consumer recurses into it), so it just reads whatever snapshot is current
// at cook time — no per-frame producer loop. app→runtime setter call = legal; no upward include.
//
// runtime leaf: pure computation, no hardware / no UI / no upward dep. The snapshot is a plain value copy
// (SpectrumSnapshot is trivially copyable); single-threaded main-thread publish + cook (the same thread
// frame_cook + the pull run on).
#include "runtime/playback_fft.h"

#include <vector>

#include "runtime/floatlist_op_registry.h"  // FloatListOp / FloatListCookCtx / floatListParam
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget

namespace sw {

namespace {

// Process-current spectrum snapshot (published by the app each frame; read by every PlaybackFFT cook this
// frame). Defaults to all-zero (a silent analyzer / no capture) → PlaybackFFT emits a zero-filled list of
// the mode's natural length, NOT an empty list: sw's analyzer buffers always exist (fixed std::array),
// unlike TiXL's nullable BASS buffers. The EmptyList branch (cs:25/28/…) only fires in TiXL when the
// analyzer never initialized; sw's arrays are always sized, so the faithful sw behavior is the zero list.
SpectrumSnapshot g_current{};

// Copy the selected snapshot array into `out`, per InputBand mode. Verbatim PlaybackFFT.cs:22-41 mapping.
void fillFromMode(const SpectrumSnapshot& s, int mode, std::vector<float>& out) {
  out.clear();
  switch (mode) {
    case 0:  // RawFft               → FftGainBuffer         (1024 bins)
      out.assign(s.fftGain.begin(), s.fftGain.end());
      break;
    case 1:  // NormalizedFft        → FftNormalizedBuffer   (1024 bins)
      out.assign(s.fftNormalized.begin(), s.fftNormalized.end());
      break;
    case 2:  // FrequencyBands       → FrequencyBands        (32 bands)
      out.assign(s.bands.begin(), s.bands.end());
      break;
    case 3:  // FrequencyBandsPeaks  → FrequencyBandPeaks    (32 bands)
      out.assign(s.peaks.begin(), s.peaks.end());
      break;
    case 4:  // FrequencyBandsAttacks→ FrequencyBandAttacks  (32 bands)
      out.assign(s.attacks.begin(), s.attacks.end());
      break;
    default:  // (unreachable: InputBand is clamped 0..4 below, matching PlaybackFFT.cs:21 .Clamp)
      break;
  }
}

// PlaybackFFT cook: read InputBand (clamp 0..4, PlaybackFFT.cs:21), copy the matching current-snapshot
// array onto the FloatList output (cs:22-43). No FloatList inputs (pure producer, like FloatsToList).
void cookPlaybackFFT(FloatListCookCtx& c) {
  if (!c.output) return;
  // InputBand is a resolved Float param; clamp to the InputModes range [0, 4] (PlaybackFFT.cs:21).
  float raw = floatListParam(c.params, "InputBand", 0.0f);
  int mode = (int)(raw + (raw >= 0.0f ? 0.5f : -0.5f));  // round-to-nearest (matches int cast of .GetValue)
  if (mode < 0) mode = 0;
  if (mode > 4) mode = 4;
  fillFromMode(g_current, mode, *c.output);

  // Test-only: corrupt the REAL output on the actual cook path (drop the last element) so the golden's RED
  // case bites here, NOT by flipping the expected value. Off in production. (mirror of FloatsToList.)
  if (floatListInjectBug() && !c.output->empty())
    c.output->pop_back();
}

}  // namespace

void setCurrentSpectrum(const SpectrumSnapshot& s) { g_current = s; }

// Self-registration. File-scope static FloatListOp — independent leaf (no shared edit point). Feeds
// floatListSpecSink() + floatListCookFns() during pre-main dynamic init.
//   Ports: OUTPUT FIRST → "Result" = the FloatList output (host-list currency, the down-rail product).
//          "InputBand"  = the enum selector (single scalar Float; read as a resolved param, NOT gathered).
// PlaybackFFT.t3 default InputBand = 0 (RawFft). STATELESS (reads only the current snapshot + its param).
static const FloatListOp _reg_playbackfft{
    {"PlaybackFFT", "PlaybackFFT",
     {{"Result", "Result", "FloatList", false},
      {"InputBand", "InputBand", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"RawFft", "NormalizedFft", "FrequencyBands", "FrequencyBandsPeaks", "FrequencyBandsAttacks"}, true}},
     /*evaluate=*/nullptr},  // FloatList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPlaybackFFT};

}  // namespace sw
