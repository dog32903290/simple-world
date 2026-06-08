// runtime/audio_ingest — the single boundary where external sound becomes
// frame-domain input (AUDIO_INGEST_CONTRACT.md). Pure computation, zero UI,
// zero Metal, zero realtime audio: this runtime owns no audio callback and no
// sample clock. It folds a timestamped stream of external SemanticPath messages
// (OSC/MIDI: SetValue / NotesState / Pulse / Connect / Disconnect) into a
// per-frame AudioInput that a visual node consumes without knowing the source.
#pragma once
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sw {

// One timestamped message from the external audio host. `t` is the host's
// wall-clock seconds; the ingest samples these at the visual frame clock.
struct AudioMessage {
  enum class Kind { Connect, Disconnect, SetValue, NotesState, Pulse };
  double t = 0.0;
  Kind kind = Kind::SetValue;
  int trackId = 0;
  std::string param;        // SetValue: continuous param name (e.g. "cutoff")
  float value = 0.0f;       // SetValue: value. Pulse: monotonic seq (idempotent).
  std::vector<int> notes;   // NotesState: authoritative active-note set at time t
};

// What a visual node consumes for one frame. Source path (semantic vs signal)
// is intentionally NOT visible here (contract Rule 1).
struct AudioInput {
  bool connected = false;
  std::map<std::string, float> values;  // smoothed continuous, key "track/param"
  std::set<std::string> notes;          // currently-held notes, key "track/pitch"
  std::vector<std::string> events;       // fired this frame: ".../noteOn/60", ".../pulse"

  // "non-black": a graph must stay cookable even when disconnected. True iff no
  // value is NaN/inf (garbage), regardless of `connected`.
  bool cookable() const {
    for (const auto& kv : values)
      if (!std::isfinite(kv.second)) return false;
    return true;
  }
};

// Source-absent policy (contract Rule 3).
enum class AbsentPolicy { Hold, Zero, MarkInvalid };

class AudioIngest {
 public:
  struct Config {
    AbsentPolicy policy = AbsentPolicy::Hold;
    float smoothing = 0.6f;  // [0,1): fraction of old value retained per frame; 0 = instant step
    // Degenerate modes — only the selftest's injectBug sets these, to prove the
    // assertions have teeth. Never used in production.
    bool edgeNotes = false;       // ignore corrective NotesState -> dropped release strands
    bool absentGarbage = false;   // emit NaN while disconnected -> breaks cookable()
  };

  AudioIngest();  // defaults (out of line: Config's member initializers)
  explicit AudioIngest(Config cfg) : cfg_(cfg) {}

  // Consume every message with t <= frameTime (since the last call), then return
  // the AudioInput snapshot for this frame. Stateful across calls (smoothing,
  // connection, held notes, pulse de-dup all persist).
  AudioInput sampleFrame(const std::vector<AudioMessage>& log, double frameTime);

 private:
  static std::string vkey(int track, const std::string& param);
  static std::string nkey(int track, int pitch);

  Config cfg_;
  size_t cursor_ = 0;
  bool connected_ = false;
  std::map<std::string, float> target_;     // latest-wins raw targets
  std::map<std::string, float> smoothed_;    // carried across frames
  std::set<std::string> active_;             // held notes
  std::map<int, int> lastPulseSeq_;          // per-track last fired pulse seq
};

// Isolated proof (constitution Rule 5). Builds a synthetic SemanticPath log and
// asserts the three contract properties over a frame sweep:
//   1. source-absent stays cookable (non-black),
//   2. continuous values are latest-wins + smoothed (not stepped),
//   3. a dropped discrete release does not strand state (state-based self-heal),
//      and a duplicate Pulse does not double-fire (idempotent).
// injectBug flips the ingest into degenerate modes so every assertion must FAIL.
int runAudioIngestSelfTest(bool injectBug);

// Replay shell: read a recorded SemanticPath log fixture (.json) and print the
// per-frame AudioInput trace. Implemented in audio_ingest_replay.cpp (crude_json).
int runAudioIngestReplay(const std::string& fixturePath);

}  // namespace sw
