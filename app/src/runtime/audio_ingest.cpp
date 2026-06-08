#include "runtime/audio_ingest.h"

#include <cstdio>

namespace sw {

AudioIngest::AudioIngest() : cfg_(Config{}) {}

std::string AudioIngest::vkey(int track, const std::string& param) {
  return std::to_string(track) + "/" + param;
}
std::string AudioIngest::nkey(int track, int pitch) {
  return std::to_string(track) + "/" + std::to_string(pitch);
}

AudioInput AudioIngest::sampleFrame(const std::vector<AudioMessage>& log, double frameTime) {
  AudioInput out;

  // 1. Fold every message up to this frame's time into persistent state.
  while (cursor_ < log.size() && log[cursor_].t <= frameTime) {
    const AudioMessage& m = log[cursor_++];
    switch (m.kind) {
      case AudioMessage::Kind::Connect:
        connected_ = true;
        break;
      case AudioMessage::Kind::Disconnect:
        connected_ = false;
        break;
      case AudioMessage::Kind::SetValue:
        target_[vkey(m.trackId, m.param)] = m.value;  // latest-wins within/across frames
        break;
      case AudioMessage::Kind::NotesState: {
        std::set<std::string> next;
        for (int p : m.notes) next.insert(nkey(m.trackId, p));
        // additions -> noteOn
        for (const std::string& k : next)
          if (!active_.count(k)) {
            active_.insert(k);
            out.events.push_back(k + "/noteOn");
          }
        if (cfg_.edgeNotes) break;  // BUG mode: never honor removals -> dropped release strands
        // removals -> noteOff (state-based self-heal: a dropped release is
        // corrected by the next authoritative NotesState).
        for (auto it = active_.begin(); it != active_.end();) {
          const std::string& k = *it;
          bool stillHeld = next.count(k) ||
                           k.rfind(std::to_string(m.trackId) + "/", 0) != 0;  // other tracks untouched
          if (!stillHeld) {
            out.events.push_back(k + "/noteOff");
            it = active_.erase(it);
          } else {
            ++it;
          }
        }
        break;
      }
      case AudioMessage::Kind::Pulse: {
        int seq = (int)m.value;
        int& last = lastPulseSeq_[m.trackId];
        if (seq > last) {  // idempotent: a retransmit (seq <= last) does not double-fire
          last = seq;
          out.events.push_back(std::to_string(m.trackId) + "/pulse");
        }
        break;
      }
    }
  }

  // 2. Advance smoothing toward the latest targets.
  const float take = cfg_.smoothing <= 0.0f ? 1.0f : (1.0f - cfg_.smoothing);
  for (const auto& kv : target_) {
    float& s = smoothed_[kv.first];
    s += (kv.second - s) * take;
  }

  // 3. Publish, applying the source-absent policy.
  out.connected = connected_;
  out.notes = active_;
  if (connected_ || cfg_.policy == AbsentPolicy::Hold || cfg_.policy == AbsentPolicy::MarkInvalid) {
    out.values = smoothed_;  // held
  } else {                    // AbsentPolicy::Zero
    for (const auto& kv : smoothed_) out.values[kv.first] = 0.0f;
  }
  if (!connected_ && cfg_.absentGarbage && !out.values.empty())
    out.values.begin()->second = std::nan("");  // BUG mode: garbage while absent
  return out;
}

namespace {

// The synthetic SemanticPath log the selftest replays. 60fps frame clock.
std::vector<AudioMessage> selftestLog() {
  using K = AudioMessage::Kind;
  std::vector<AudioMessage> log = {
      {0.000, K::Connect, 0, "", 0.0f, {}},
      {0.000, K::SetValue, 0, "cutoff", 0.0f, {}},
      {0.005, K::NotesState, 0, "", 0.0f, {60}},     // note 60 on
      {0.010, K::SetValue, 0, "cutoff", 1.0f, {}},   // superseded same frame ->
      {0.012, K::SetValue, 0, "cutoff", 0.8f, {}},   //   latest-wins = 0.8
      {0.020, K::Pulse, 0, "", 1.0f, {}},            // pulse seq 1
      {0.021, K::Pulse, 0, "", 1.0f, {}},            // duplicate seq 1 -> ignored
      // intended release near 0.040 is DROPPED (absent from the log)
      {0.070, K::NotesState, 0, "", 0.0f, {}},       // authoritative {} -> self-heal release
      {0.100, K::Disconnect, 0, "", 0.0f, {}},
  };
  return log;
}

}  // namespace

int runAudioIngestSelfTest(bool injectBug) {
  AudioIngest::Config cfg;
  if (injectBug) {
    cfg.smoothing = 0.0f;       // breaks property 2 (step, no smoothing)
    cfg.edgeNotes = true;       // breaks property 3 (dropped release strands)
    cfg.absentGarbage = true;   // breaks property 1 (NaN while absent)
  }
  AudioIngest ingest(cfg);
  const std::vector<AudioMessage> log = selftestLog();

  // Sweep frames 0..7 at 60fps, keeping each snapshot.
  std::vector<AudioInput> frames;
  for (int i = 0; i <= 7; ++i) frames.push_back(ingest.sampleFrame(log, i / 60.0));

  const float eps = 1e-3f;
  const std::string ckey = "0/cutoff", n60 = "0/60";

  // Property 2: latest-wins (target 0.8 not 1.0) AND smoothed (0.32 not 0.8) at frame1.
  float v1 = frames[1].values.count(ckey) ? frames[1].values.at(ckey) : -1.0f;
  bool latestWins = std::fabs(v1 - 0.32f) < eps;     // 0.0 + (0.8-0.0)*0.4
  bool converges = frames[3].values.at(ckey) > frames[1].values.at(ckey) &&
                   frames[3].values.at(ckey) < 0.8f + eps;

  // Property 3: note 60 on at frame1, self-healed off by the final frame; pulse once.
  bool noteOn = frames[1].notes.count(n60) > 0;
  bool noteReleased = frames[7].notes.count(n60) == 0;
  int pulses = 0;
  for (const auto& f : frames)
    for (const auto& e : f.events)
      if (e == "0/pulse") ++pulses;
  bool pulseIdempotent = (pulses == 1);

  // Property 1: disconnected frame stays cookable, with held finite values.
  bool absentCookable = !frames[7].connected && frames[7].cookable() &&
                        !frames[7].values.empty();

  bool pass = latestWins && converges && noteOn && noteReleased && pulseIdempotent &&
              absentCookable;
  printf(
      "[selftest-audioingest] v1=%.3f latestWins=%d converge=%d noteOn=%d release=%d "
      "pulses=%d absentCookable=%d -> %s\n",
      v1, latestWins, converges, noteOn, noteReleased, pulses, absentCookable,
      pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

}  // namespace sw
