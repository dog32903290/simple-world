// runtime/audio_ingest_replay — the "ingest shell": read a recorded SemanticPath
// log fixture (.json) and print the per-frame AudioInput trace it produces.
// Split from audio_ingest.cpp so the core engine stays dependency-free (crude_json
// lives in third_party, like graph.cpp's save/load). Mirrors graph.cpp's parsing.
#include <cstdio>
#include <fstream>
#include <iterator>

#include "crude_json.h"
#include "runtime/audio_ingest.h"

namespace sw {
namespace {

AudioMessage::Kind parseKind(const std::string& s) {
  if (s == "connect") return AudioMessage::Kind::Connect;
  if (s == "disconnect") return AudioMessage::Kind::Disconnect;
  if (s == "notesState") return AudioMessage::Kind::NotesState;
  if (s == "pulse") return AudioMessage::Kind::Pulse;
  return AudioMessage::Kind::SetValue;
}

AbsentPolicy parsePolicy(const std::string& s) {
  if (s == "zero") return AbsentPolicy::Zero;
  if (s == "markInvalid") return AbsentPolicy::MarkInvalid;
  return AbsentPolicy::Hold;
}

}  // namespace

int runAudioIngestReplay(const std::string& fixturePath) {
  if (fixturePath.empty()) {
    printf("[audioingest-replay] FAIL: no fixture path (usage: --audio-ingest-replay <file.json>)\n");
    return 1;
  }
  std::ifstream f(fixturePath);
  if (!f) {
    printf("[audioingest-replay] FAIL: cannot open %s\n", fixturePath.c_str());
    return 1;
  }
  std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  crude_json::value v = crude_json::value::parse(json);
  if (!v.is_object()) {
    printf("[audioingest-replay] FAIL: malformed json in %s\n", fixturePath.c_str());
    return 1;
  }

  AudioIngest::Config cfg;
  crude_json::value& ing = v["ingest"];
  if (ing.is_object()) {
    if (ing["smoothing"].is_number()) cfg.smoothing = (float)ing["smoothing"].get<crude_json::number>();
    if (ing["policy"].is_string()) cfg.policy = parsePolicy(ing["policy"].get<crude_json::string>());
  }
  AudioIngest ingest(cfg);

  std::vector<AudioMessage> log;
  crude_json::value& msgs = v["messages"];
  if (!msgs.is_array()) {
    printf("[audioingest-replay] FAIL: `messages` is not an array\n");
    return 1;
  }
  for (auto& mv : msgs.get<crude_json::array>()) {
    AudioMessage m;
    if (mv["t"].is_number()) m.t = mv["t"].get<crude_json::number>();
    m.kind = parseKind(mv["kind"].is_string() ? mv["kind"].get<crude_json::string>() : "");
    if (mv["track"].is_number()) m.trackId = (int)mv["track"].get<crude_json::number>();
    if (mv["param"].is_string()) m.param = mv["param"].get<crude_json::string>();
    if (mv["value"].is_number()) m.value = (float)mv["value"].get<crude_json::number>();
    crude_json::value& notes = mv["notes"];
    if (notes.is_array())
      for (auto& nv : notes.get<crude_json::array>()) m.notes.push_back((int)nv.get<crude_json::number>());
    log.push_back(m);
  }

  int fps = 60, frames = 8;
  crude_json::value& fc = v["frameClock"];
  if (fc.is_object()) {
    if (fc["fps"].is_number()) fps = (int)fc["fps"].get<crude_json::number>();
    if (fc["frames"].is_number()) frames = (int)fc["frames"].get<crude_json::number>();
  }

  printf("[audioingest-replay] %s  fps=%d frames=%d msgs=%zu\n", fixturePath.c_str(), fps, frames,
         log.size());
  for (int i = 0; i < frames; ++i) {
    AudioInput in = ingest.sampleFrame(log, (double)i / fps);
    printf("frame %d t=%.4f connected=%d cookable=%d", i, (double)i / fps, in.connected,
           in.cookable());
    for (const auto& kv : in.values) printf("  %s=%.3f", kv.first.c_str(), kv.second);
    if (!in.notes.empty()) {
      printf("  notes={");
      for (const auto& n : in.notes) printf("%s ", n.c_str());
      printf("}");
    }
    for (const auto& e : in.events) printf("  [%s]", e.c_str());
    printf("\n");
  }
  return 0;
}

}  // namespace sw
