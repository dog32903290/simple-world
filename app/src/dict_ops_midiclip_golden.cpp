// dict_ops_midiclip_golden — --selftest-midiclipnode. THE MidiClip NODE-WIRING golden (dataset seam →
// dict-currency rail). The core fold (parseSmf + accumulateMidiClip) is already proven by
// --selftest-midiclip (midiclip_golden.cpp). THIS golden proves the NEW seam the leaf adds: the DictOp
// producer cook (dict_ops_midiclip.cpp cookMidiClip driven through cookFlatDict) —
//   (1) the Filename String input → a real .mid file on disk (readFileBytes + resolveMidiPath),
//   (2) the bars→ticks conversion from EvaluationContext.localFxTime (MidiClip.cs:144), and
//   (3) the DictOp registration (findDictOp("MidiClip") resolves; the node appears on the Dict rail).
// It drives cookFlatDict on a real Graph node whose "Filename" strParam points at a hand-built SMF temp
// file, at DIVERGENT playhead bars, and asserts the folded SwFloatDict.
//
// FIXTURE (same 4-event SMF as midiclip_golden, PPQN 480): NoteOn C4 @0 vel100, CC74 @0 val64, NoteOn G4
// @240 vel127, NoteOff C4 @480. bars→ticks = bars * 4 * 480 = bars * 1920.
//
// EXPECTED (hand-derived from MidiClip.cs:144 + accumulateMidiClip key/value scheme, GOLDEN_STANDARD):
//   • bars 0.15625 → tick 300 (between G4 and NoteOff): C4=100/127=0.7874, cc74=0.5039, G4=1.0.
//   • bars 0.260417 → tick 500 (after NoteOff):         C4=0.0, cc74=0.5039, G4=1.0.
//   • bars 0.0520833 → tick 100 (before G4):            C4=0.7874, cc74=0.5039, NO G4 key (tick gate).
//   Divergent bars exercise the bars→ticks math + the tick gate + last-write-wins THROUGH the node cook.
//
// TOOTH (one real transform seam, NOT want-flip): midiClipInjectBug() (midi_smf.h:74) skips the /127
// normalisation on the REAL fold the cook calls → the node's dict carries raw 100 ≠ 0.7874. Bites the
// value transform on the actual cook path (the same seam the core golden bites, now reached VIA the node).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>  // getpid

#include "runtime/dict_cook.h"         // cookFlatDict
#include "runtime/dict_op_registry.h"  // SwFloatDict / findDictOp / dictInjectBug
#include "runtime/eval_context.h"      // EvaluationContext (localFxTime = bars)
#include "runtime/graph.h"             // Graph / Node
#include "runtime/midi_smf.h"          // midiClipInjectBug

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-3f; }

void putVlq(std::vector<uint8_t>& b, uint32_t v) {
  uint8_t stack[4];
  int n = 0;
  stack[n++] = v & 0x7F;
  v >>= 7;
  while (v) { stack[n++] = (v & 0x7F) | 0x80; v >>= 7; }
  for (int k = n - 1; k >= 0; --k) b.push_back(stack[k]);
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
  b.push_back((v >> 8) & 0xFF);  b.push_back(v & 0xFF);
}
void putU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }

std::vector<uint8_t> buildFixtureSmf() {
  std::vector<uint8_t> trk;
  putVlq(trk, 0);   trk.push_back(0x90); trk.push_back(60);  trk.push_back(100);  // NoteOn  C4 @0
  putVlq(trk, 0);   trk.push_back(0xB0); trk.push_back(74);  trk.push_back(64);   // CC74      @0
  putVlq(trk, 240); trk.push_back(0x91); trk.push_back(67);  trk.push_back(127);  // NoteOn  G4 @240
  putVlq(trk, 240); trk.push_back(0x80); trk.push_back(60);  trk.push_back(0);    // NoteOff C4 @480
  putVlq(trk, 0);   trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00); // EoT
  std::vector<uint8_t> b;
  b.push_back('M'); b.push_back('T'); b.push_back('h'); b.push_back('d');
  putU32(b, 6); putU16(b, 0); putU16(b, 1); putU16(b, 480);
  b.push_back('M'); b.push_back('T'); b.push_back('r'); b.push_back('k');
  putU32(b, (uint32_t)trk.size());
  b.insert(b.end(), trk.begin(), trk.end());
  return b;
}

// Write the fixture .mid to a unique temp path (mirror loadsvg_golden::writeTempSvg).
std::filesystem::path writeTempMid() {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) return {};
  std::filesystem::path file = base / ("sw_midiclip_node_" + std::to_string(::getpid()) + ".mid");
  std::ofstream o(file, std::ios::binary);
  if (!o.is_open()) return {};
  std::vector<uint8_t> smf = buildFixtureSmf();
  o.write(reinterpret_cast<const char*>(smf.data()), (std::streamsize)smf.size());
  o.close();
  return file;
}

// Drive the MidiClip DictOp cook through cookFlatDict on a real one-node graph: node id 1, type
// "MidiClip", Filename strParam = the temp path, playhead = `bars` (EvaluationContext.localFxTime).
SwFloatDict cookNode(const std::string& path, double bars, bool injectBug) {
  Graph g;
  Node n; n.id = 1; n.type = "MidiClip"; n.strParams["Filename"] = path;
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.localFxTime = (float)bars;
  ctx.time = 0.0f;
  SwFloatDict out;
  midiClipInjectBug() = injectBug;
  cookFlatDict(g, ctx, 1, out);
  midiClipInjectBug() = false;
  return out;
}

float get(const SwFloatDict& d, const std::string& key) {
  float v = -999.0f;
  d.tryGet(key, v);
  return v;
}

}  // namespace

int runMidiClipNodeSelfTest(bool injectBug) {
  bool ok = true;

  // ── LEG 0 — the DictOp is registered on the rail (the node appears in findSpec/findDictOp). Structural,
  //    non-bug: a missing registration would make cookFlatDict return false (empty dict) below anyway. ──
  if (!injectBug) {
    bool pass = findDictOp("MidiClip") != nullptr;
    ok = ok && pass;
    std::printf("[selftest-midiclipnode] LEG0 registered: %s\n", pass ? "PASS" : "FAIL");
  }

  std::filesystem::path mid = writeTempMid();
  if (mid.empty()) {
    std::printf("[selftest-midiclipnode] SETUP FAIL (temp .mid)\n");
    return injectBug ? 0 : 1;  // setup failure on the clean leg is a real FAIL; bug leg can't bite → 0.
  }
  const std::string path = mid.string();

  // ── LEG 1 — cook @ bars 0.15625 (tick 300, before NoteOff): C4=0.7874, cc74=0.5039, G4=1.0. The /127
  //    tooth rides C4 through the NODE cook → -bug (raw 100) reddens. Divergent playhead (P2). ──
  {
    SwFloatDict d = cookNode(path, 0.15625, injectBug);
    float c4 = get(d, "/channel0/C4");
    float cc = get(d, "/channel0/controller74");
    float g4 = get(d, "/channel1/G4");
    bool pass = nearf(c4, 100.0f / 127.0f) && nearf(cc, 64.0f / 127.0f) && nearf(g4, 1.0f);
    ok = ok && pass;
    std::printf("[selftest-midiclipnode] LEG1 cook@bars0.15625: C4=%.4f (want 0.7874) cc=%.4f G4=%.4f -> %s\n",
                c4, cc, g4, pass ? "PASS" : "FAIL");
  }

  // ── LEG 2 — cook @ bars 0.260417 (tick 500, after NoteOff): C4=0.0 (overwritten), cc74=0.5039, G4=1.0.
  //    Tooth rides cc74 (0.5039) so -bug reddens even though C4=0 both ways. NoteOff-overwrite load-bearing. ──
  {
    SwFloatDict d = cookNode(path, 0.260417, injectBug);
    float c4 = get(d, "/channel0/C4");
    float cc = get(d, "/channel0/controller74");
    float g4 = get(d, "/channel1/G4");
    bool pass = nearf(c4, 0.0f) && nearf(cc, 64.0f / 127.0f) && nearf(g4, 1.0f);
    ok = ok && pass;
    std::printf("[selftest-midiclipnode] LEG2 cook@bars0.260417: C4=%.4f (want 0) cc=%.4f (want 0.5039) G4=%.4f -> %s\n",
                c4, cc, g4, pass ? "PASS" : "FAIL");
  }

  // ── LEG 3 — cook @ bars 0.0520833 (tick 100, before G4 @240): C4=0.7874, cc74=0.5039, NO G4 key. The
  //    bars→ticks conversion + tick gate are load-bearing (a wrong conversion would let G4 leak in early).
  //    Pure boundary, non-bug. ──
  if (!injectBug) {
    SwFloatDict d = cookNode(path, 0.0520833, false);
    bool hasG4 = d.has("/channel1/G4");
    bool pass = !hasG4 && nearf(get(d, "/channel0/C4"), 100.0f / 127.0f) && d.count() == 2;
    ok = ok && pass;
    std::printf("[selftest-midiclipnode] LEG3 cook@bars0.052: keys=%zu G4present=%d -> %s\n",
                d.count(), hasG4, pass ? "PASS" : "FAIL");
  }

  std::error_code ec;
  std::filesystem::remove(mid, ec);

  std::printf("[selftest-midiclipnode] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
