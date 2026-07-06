// app/frame_cook_link — AbletonLinkSync per-frame cook (product behaviour, split out of frame_cook.cpp
// to respect the line-count ratchet). Zone: app. Owns the ONE platform LinkSync session (leaf-seam:
// platform owns the native Link handle, THIS app layer owns the meaning + the node cook).
//
// = TiXL AbletonLinkSync.cs Update() (cs:25-99): read the live session (beat/phase/tempo/quantum/
// peerCount), run the trigger edges (start/stop/reconnect), select Result by OutputType, honour
// PauseIfDisconnected. The native session lives in platform/link_sync (asio-isolated TU); this cook
// reads a plain LinkSnapshot value struct — no threading, no asio in this file.
//
// Externally-cooked node (like AudioReaction): the NodeSpec has evaluate==nullptr; this writes extOut
// (Result/Tempo/IsConnected) which evalResidentFloat reads back through the generic no-evaluate path.
#include "app/frame_cook.h"

#include <map>
#include <string>

#include "platform/link_sync.h"           // LinkSync / LinkSnapshot (app->platform, legal)
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / resolveResidentFloatInputs
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

// Per-node trigger edge latches (WasTriggered's `ref current`) keyed by resident path. Start/Stop/
// Reconnect each fire once on a false→true edge, exactly like the SetKeyframes latch.
struct LinkNodeState {
  bool prevStart = false, prevStop = false, prevReconnect = false;
};

// The ONE app-owned Link session (lazy — constructed on the first AbletonLinkSync cook so a graph with
// no Link node never opens a socket). enable(true) once so the local timeline runs (TiXL TryInitialize +
// EnableStartStopSync). A single session is correct: TiXL's native handle is a static singleton
// (_nativeLinkInstance, cs:288) shared by every AbletonLinkSync instance.
LinkSync& session() {
  static LinkSync s(120.0);
  static bool inited = false;
  if (!inited) {
    s.enable(true);
    s.enableStartStopSync(true);
    inited = true;
  }
  return s;
}

// TiXL ReturnTypes (cs:278-285): the OutputType enum ordinals.
enum ReturnTypes { kBars = 0, kPhase = 1, kBeats = 2, kTime = 3, kQuantum = 4 };

}  // namespace

// Cook every AbletonLinkSync instance in `g` for this frame. Reads the live LinkSnapshot + the node's
// resolved Float params, runs the trigger edges, selects Result by OutputType, writes extOut
// [0]=Result [1]=Tempo [2]=IsConnected. Mirrors AudioReaction's externally-cooked shape.
void cookLinkSyncNodes(ResidentEvalGraph& g, const Transport& t, uint32_t frameIndex,
                       const SymbolLibrary* lib) {
  // Only touch the session if a Link node actually exists (don't open a socket for an unrelated graph).
  bool anyLink = false;
  for (const ResidentNode& rn : g.nodes)
    if (rn.opType == "AbletonLinkSync") { anyLink = true; break; }
  if (!anyLink) return;

  static std::map<std::string, LinkNodeState> s_state;  // per-path trigger latches (app-owned)

  ResidentEvalCtx rctx;
  rctx.localTime = (float)t.position;
  rctx.localFxTime = (float)t.fxTime;
  rctx.frameIndex = frameIndex;
  rctx.lib = lib;

  LinkSync& link = session();

  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "AbletonLinkSync") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    LinkNodeState& st = s_state[rn.path];

    // Trigger edges (WasTriggered, MathUtils.cs:531-538): fire on false→true, then the input is reset
    // to false (TiXL SetTypedInputValue(false)). sw has no per-frame input write-back to the lib here, so
    // we edge-latch on the resolved value; a wire HELD high fires once (correct) — a constant-true
    // override would re-latch only when toggled, matching TiXL's post-fire reset.
    const bool startNow = P["TriggerStartPlaying"] >= 0.5f;
    const bool stopNow = P["TriggerStopPlaying"] >= 0.5f;
    const bool reconnectNow = P["TriggerReconnect"] >= 0.5f;
    if (startNow && !st.prevStart) link.startPlaying();      // cs:33-37
    if (stopNow && !st.prevStop) link.stopPlaying();         // cs:39-43
    if (reconnectNow && !st.prevReconnect) link.enable(true);// cs:46-50 (sw reconnect = re-enable comms)
    st.prevStart = startNow;
    st.prevStop = stopNow;
    st.prevReconnect = reconnectNow;

    const LinkSnapshot s = link.snapshot();
    const bool pauseIfDisconnected = P["PauseIfDisconnected"] >= 0.5f;
    const bool pauseResults = pauseIfDisconnected && s.peerCount == 0;  // cs:60-61

    rn.extOut[2] = s.isConnected ? 1.0f : 0.0f;  // IsConnected (cs:28) — always emitted
    if (!s.isConnected) { rn.extOut[0] = 0.0f; rn.extOut[1] = 0.0f; continue; }  // cs:30-31 early-out

    if (!pauseResults) {  // cs:63-75: with _startMeasure forced to 0 (cs:57)
      rn.extOut[1] = (float)s.tempo;  // Tempo
      const int outType = (int)(P["OutputType"] + 0.5f);
      float result = 0.0f;
      switch (outType) {
        case kBars:    result = (float)(s.beat / s.quantum); break;  // beat/quantum - 0
        case kPhase:   result = (float)s.phase; break;
        case kBeats:   result = (float)s.beat; break;               // beat - 0
        case kTime:    result = (float)(s.timeMicros / 1.0e6); break;  // µs → seconds (TiXL time/1000 ms→s)
        case kQuantum: result = (float)s.quantum; break;
        default:       result = 0.0f; break;
      }
      rn.extOut[0] = result;
    }
    // pauseResults → leave Result/Tempo at their prior extOut (frozen), = TiXL not writing them (cs:63).
  }
}

}  // namespace sw::framecook
