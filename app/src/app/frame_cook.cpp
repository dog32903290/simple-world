#include "app/frame_cook.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/audio_monitor.h"            // live spectrum snapshot (DSP fed by capture)
#include "app/cook_host_values.h"         // cookHostValueNodes (String/host-scalar/ColorList cook)
#include "app/document.h"                 // g_lib + libRevision (projection contract)
#include "app/soundtrack.h"               // soundtrack follow rule (audio chases the transport)
#include "runtime/audio_reaction.h"       // cookAudioReaction (TiXL AudioReaction parity)
#include "runtime/graph_bridge.h"         // refreshCompoundSpecs (frame-boundary spec swap)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/point_graph.h"          // PointGraph::cookResident
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph
#include "runtime/spectrum_analyzer.h"    // SpectrumSnapshot
#include "runtime/stateful_value_ops.h"   // cookStatefulValueOp (Damp/Spring/... value-graph sims)
#include "runtime/transport.h"            // Transport (two-clock playback head, S5)

namespace sw::framecook {
namespace {

// App clock (was main's g_time/g_frameIndex — product behaviour, so it lives here).
uint32_t g_frameIndex = 0;

// S5: the FrameScheduler's wall-clock. The real per-frame deltaTime (NOT a hardcoded 1/60) is
// measured HERE at the frame boundary and handed to the Transport — this app-layer driver is the
// "FrameScheduler := graph-level owner of frameIndex/time/deltaTime" (FRAME_SCHEDULER_CONTRACT):
// one dt per frame, every cooked node downstream reads the SAME ctx built from it.
Transport g_transport;
bool g_haveLastFrame = false;
std::chrono::steady_clock::time_point g_lastFrameTp;

// Pull dt from the wall clock (steady_clock — monotonic, immune to NTP). First frame has no prior
// timestamp -> dt = 0 (no spurious jump). NO ceiling here: this is the TRUE wall dt and the
// transport must eat it whole — TiXL Playback advances TimeInBars from a Stopwatch, unclamped.
// Clamping it made a stalled frame (window drag, debugger) advance the playhead only 0.25s while
// the audio free-ran the full stall on its own thread, so drift = stall−0.25 and the follow rule
// hard-seeked the soundtrack BACKWARDS, audibly (refuter-C 修2). The 0.25 ceiling survives only
// on the SIM leg — see simDeltaFromWall below.
double measureDeltaSeconds() {
  auto now = std::chrono::steady_clock::now();
  if (!g_haveLastFrame) { g_lastFrameTp = now; g_haveLastFrame = true; return 0.0; }
  double dt = std::chrono::duration<double>(now - g_lastFrameTp).count();
  g_lastFrameTp = now;
  if (dt < 0.0) dt = 0.0;
  return dt;
}

// PRODUCTION (compound spine, lib-native since 批次 3 N2): the frame cook walks this
// RESIDENT eval graph, projected straight from doc::g_lib whenever doc::libRevision()
// changes (rebuild-on-edit; paths == child-id chains, so per-path GPU buffers + stateful op
// state SURVIVE the rebuild). Incremental patch wiring (patch*/patchLib*) replaces the
// rebuild in a later cut — semantics already pinned by the patch goldens.
ResidentEvalGraph g_residentGraph;
uint64_t g_builtRev = 0;  // doc::libRevision() the projection was built from (0 = never)

// editor-only: the transitive closure of live-source nodes (Time/Automation) along the
// ResidentEvalGraph's Connection graph. Rebuilt once per lib revision (same cadence as the
// graph itself). Used by stampLiveLastUpdatePass to stamp EVERY node reachable from a live
// source, not just the source itself — so Time→Multiply: Multiply is in this set and gets
// stamped every frame (= TiXL DirtyFlag.SetUpdated on every dirty slot in Slot.cs:160-168).
// Static nodes (outside the closure) keep lastUpdatePass=0 → fade after 60 frames (fork-I0,
// named; TiXL never has rebuild so its every-dirty-slot stamp is exact; ours approximates by
// pre-computing reachability once rather than tracking which nodes actually recomputed).
std::unordered_set<std::string> g_liveDownstreamPaths;  // node paths reachable from a live source

// Rebuild g_liveDownstreamPaths by delegating to the runtime computeLiveDownstreamClosure.
// Called once per lib revision, after initResidentCache (which sets isLiveSource).
void rebuildLiveDownstreamClosure(const ResidentEvalGraph& g) {
  g_liveDownstreamPaths = computeLiveDownstreamClosure(g);
}

}  // namespace

// The dt 分流 seam (refuter-C 修2): one wall measurement, two consumers with different physics.
// The TRANSPORT gets the raw dt (playhead truth — audio follows it, so it must cover the whole
// stall); the Metal SIM integration step gets this clamped copy (a 2s Euler step explodes
// particles; 0.25 was always a sim-stability number, never a playhead rule). Pinned headless by
// --selftest-arclock leg ③ + --selftest-transport (unclamped advance).
double simDeltaFromWall(double dtSecs) {
  if (dtSecs < 0.0) return 0.0;
  return dtSecs > 0.25 ? 0.25 : dtSecs;
}

const float* residentOut(const char* path) {
  const ResidentNode* n = g_residentGraph.node(path);
  return n ? n->extOut : nullptr;
}

// editor-only: max lastUpdatePass across ALL outputs of the node at `path`.
// Returns currentFrameIndex() (= just-updated) when the node is not resident or has no
// output cache — treating unknown nodes as freshly updated so they never fade (誠實邊界).
uint32_t residentNodeLastUpdatePass(const char* path) {
  const ResidentNode* n = g_residentGraph.node(path);
  if (!n || n->outCache.empty()) return g_frameIndex;  // no resident record -> treat as fresh
  uint32_t maxPass = 0;
  for (const auto& kv : n->outCache) {
    if (kv.second.lastUpdatePass > maxPass) maxPass = kv.second.lastUpdatePass;
  }
  return maxPass;
}

uint32_t currentFrameIndex() { return g_frameIndex; }

// editor-only: stamp lastUpdatePass = frameIndex on every output of nodes in the live-source
// downstream closure (g_liveDownstreamPaths). This covers BOTH the live sources themselves
// (Time / Automation-driven) AND every downstream node reachable via Connection edges —
// e.g. Time→Multiply: Multiply is in the closure and gets stamped every frame.
//
// Without the closure, only isLiveSource nodes were stamped; downstream stateless nodes
// (evalResidentFloat path, no cache write) kept lastUpdatePass=0 and faded after 60 frames
// even though they recompute every frame (root cause: TiXL Slot.cs:160-168 calls SetUpdated
// on every dirty slot, not only live sources; our stateless eval path has no equivalent).
//
// Static nodes (outside the closure) keep their lastUpdatePass (0 = never, or from
// pullResidentFloat in selftests) → fade after 60 frames. Fork-I0: TiXL has no rebuild so
// its every-dirty-slot stamp is exact; we approximate by pre-computing reachability once per
// revision, which is equivalent for the production evalResidentFloat path (stateless, so every
// node reachable from a live source truly recomputes every frame).
void stampLiveLastUpdatePass(ResidentEvalGraph& g, uint32_t frameIndex) {
  for (ResidentNode& n : g.nodes) {
    if (g_liveDownstreamPaths.count(n.path) == 0) continue;
    for (auto& kv : n.outCache)
      kv.second.lastUpdatePass = frameIndex;
  }
}

// Cook every AudioReaction instance (TiXL parity): resolve its params through the resident
// drivers (override/default/wire — the slice-2b seam), run the stateful algorithm on the
// live spectrum, write its 3 outputs onto the resident node's extOut — the resident cook's
// value wires read these (evalResidentFloat's no-evaluate path), and the UI face reads
// them back through residentOut(). State keys off the resident PATH, so it survives
// rebuilds AND stays per-instance inside compounds.
void cookAudioReactionNodes(ResidentEvalGraph& g, const SpectrumSnapshot& spec,
                            const Transport& t, uint32_t frameIndex, const SymbolLibrary* lib,
                            std::map<std::string, AudioReactionState>& state) {
  ResidentEvalCtx rctx;
  rctx.localTime = (float)t.position;  // playhead (bars) — automation sampling reads this
  rctx.localFxTime = (float)t.fxTime;  // wall clock (bars) — the Time op's evaluate() reads this
  rctx.frameIndex = frameIndex;
  rctx.lib = lib;                      // S3 接通: Automation drivers resolve curves THROUGH the lib
  // AudioReaction eats fxTime in BARS — TiXL's LocalFxTime IS Playback.FxTimeInBars
  // (EvaluationContext.cs:49) and MinTimeBetweenHits compares in the same domain; feeding
  // seconds skews the debounce window at any BPM != 240 (refuter-S5 BROKEN-A). Pinned by
  // --selftest-arclock — change this domain and that tooth bites.
  const double fxBars = t.fxTime;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "AudioReaction") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    AudioReactionParams p;
    p.amplitude = P["Amplitude"];
    p.inputBand = (int)(P["InputBand"] + 0.5f);
    p.windowCenter = P["WindowCenter"];
    p.windowWidth = P["WindowWidth"];
    p.windowEdge = P["WindowEdge"];
    p.threshold = P["Threshold"];
    p.minTimeBetweenHits = P["MinTimeBetweenHits"];
    p.output = (int)(P["Output"] + 0.5f);
    p.bias = P["Bias"];
    p.reset = P["Reset"] > 0.5f;
    // Wall clock: AudioReaction is a stateful sim — its hit timing must NOT freeze on
    // pause (L8 fxTime brother). Frozen only when both clocks frozen (no idle motion).
    const AudioReactionOut o = cookAudioReaction(spec, p, fxBars, state[rn.path]);
    rn.extOut[0] = o.level;
    rn.extOut[1] = o.wasHit ? 1.0f : 0.0f;
    rn.extOut[2] = (float)o.hitCount;
  }
}

// Cook every stateful value node (Damp/Spring/...) — the value-graph sibling of
// cookAudioReactionNodes. Same shape: resolve each node's Float inputs through the resident drivers
// (so a Damp.Value wired from Time/Automation walks the curve), step the per-instance state (keyed
// by resident PATH so it survives projection rebuilds AND stays per-instance inside compounds),
// write the outputs onto extOut. evalResidentFloat reads them back via the generic no-evaluate path.
// `dtSecs` is the RAW wall delta (TiXL Damp/Spring sample Playback.LastFrameDuration); each op
// clamps internally as TiXL does (Damp's spring branch clamps to 1/60; Spring uses no dt).
//
// context-var YELLOW seam (block #1): `vars` is the host-side per-frame variable map (= TiXL
// EvaluationContext.Float/IntVariables). The single g.nodes loop is split into THREE phases so the
// writer-before-reader ordering is deterministic every frame (simple_world iterates build order, not
// dataflow — TiXL's structural SubGraph-pull ordering must be imposed explicitly):
//   pass 0: CLEAR the map once (= EvaluationContext.Reset, cs:43-58) — the per-frame scratchpad.
//   pass 1: WRITERS (isContextVarWriter: Set*Var) — populate the map.
//   pass 2: everyone else (Get*Var readers + every non-var stateful op) — read the populated map.
// BOUNDARY (named): two passes = exactly ONE write-generation; a Set→Get→Set chain in one frame is
// NOT supported (that needs topological/scope order = RED). Verified no Set*Var VALUE input resolves
// through a Get*Var extOut in the proving graph (the resident drivers feed Set*Var from
// constants/Time, never from a Get*Var output) — so two passes suffice for the YELLOW tier.
//
// `ctxVarBug` is a TEETH hook (0 = production): 1 collapses the 2 passes into one in-order loop
// (the C ordering golden bites), 2 skips the pass-0 clear (the D per-frame-reset golden bites). It
// defaults to 0 so run() and every other caller are unchanged.
void cookStatefulValueNodes(ResidentEvalGraph& g, float dtSecs, float timeSecs, double runTimeSecs,
                            const Transport& t, uint32_t frameIndex, const SymbolLibrary* lib,
                            std::map<std::string, StatefulValueState>& state,
                            ContextVarMap& vars, int ctxVarBug) {
  ResidentEvalCtx rctx;
  rctx.localTime = (float)t.position;    // playhead (bars) — automation sampling reads this
  rctx.localFxTime = (float)t.fxTime;    // wall clock (bars)
  rctx.frameIndex = frameIndex;
  rctx.lib = lib;
  // Read-only transport snapshot for the transport-reading ops (StopWatch). The run clock is the
  // process-lifetime wall accumulator (TiXL Playback.RunTimeInSecs analog, R-1 fork — see the op);
  // bars<->secs uses the transport bpm (transport.h:37). Filled host-side; NEVER reaches the GPU
  // EvaluationContext (eval_context.h:39 16-byte lock). This is the ONLY frame_cook touch for the seam.
  TransportSnapshot tr;
  tr.runTimeSecs = runTimeSecs;
  tr.localTimeBars = t.position;
  tr.localFxTimeBars = t.fxTime;
  tr.playbackTimeBars = t.position;
  tr.bpm = t.bpm;
  tr.rate = t.rate;

  // pass 0: clear the var map ONCE at the top (= EvaluationContext.Reset). injectBug 2 skips it so a
  // stale value from a previous frame leaks (the D golden's RED path).
  if (ctxVarBug != 2) {
    vars.floatVars.clear();
    vars.intVars.clear();
  }

  // Cook one node (resolve Float inputs + the String VariableName, step, write extOut).
  auto cookOne = [&](ResidentNode& rn) {
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    // context-var ops read the resolved String VariableName off the resident node's strInputs (the
    // string sub-seam channel; empty for every non-var op → harmless). NOT smuggled through a float.
    std::string varName;
    auto vit = rn.strInputs.find("VariableName");
    if (vit != rn.strInputs.end()) varName = vit->second;
    float out[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};  // ≥3-output ops (HasVec3Changed=7)
    cookStatefulValueOp(rn.opType, P, dtSecs, timeSecs, state[rn.path], out, tr, &vars, varName);
    for (int i = 0; i < 8; ++i) rn.extOut[i] = out[i];
  };

  if (ctxVarBug == 1) {
    // injectBug 1: collapse to a single in-order loop (no writer-before-reader guarantee). A
    // Get*Var declared before its Set*Var writer then reads the fallback (the C golden bites).
    for (ResidentNode& rn : g.nodes)
      if (isStatefulValueOp(rn.opType)) cookOne(rn);
    return;
  }

  // pass 1: WRITERS (Set*Var) — every writer runs before any reader, deterministically.
  for (ResidentNode& rn : g.nodes)
    if (isStatefulValueOp(rn.opType) && isContextVarWriter(rn.opType)) cookOne(rn);
  // pass 2: readers + all other stateful ops.
  for (ResidentNode& rn : g.nodes)
    if (isStatefulValueOp(rn.opType) && !isContextVarWriter(rn.opType)) cookOne(rn);
}

void run(PointGraph& pg, const std::string& targetPath) {
  // Frame start = THE defined point where the composition path is validated (document.h):
  // every panel drawn after this agrees on the current symbol; mid-frame edits dangle the
  // tail at most until the next frame.
  doc::validateCompositionPath();

  // Projection rebuild BEFORE the AudioReaction cooker, so extOut lands on the fresh graph.
  if (g_builtRev != doc::libRevision()) {
    // Refresh the dynamic compound-spec table HERE, at the frame boundary — never from the
    // command stack mid-frame, where the inspector/canvas hold NodeSpec* into the table
    // (swap = use-after-free, refuter N2 #1). The UI reads one-frame-stale compound specs
    // after an edit, same frame semantics as TiXL.
    refreshCompoundSpecs(doc::g_lib);
    ResidentEvalGraph fresh = buildEvalGraph(doc::g_lib, doc::g_lib.rootId);
    // Initialise per-output caches (sets isLiveSource, base/source/value versions) BEFORE
    // transplantDisabledCaches — transplant reads outCache.isDisabled, which is 0-initialized
    // without this call (meaning no frozen values would ever ride across a rebuild, silently
    // breaking the S2 disable-freeze guarantee). The production path walks evalResidentFloat
    // (stateless, no version-chasing pull), so bumpLiveSources is NOT needed here — the cache
    // is used only for the idle-fade signal (lastUpdatePass) and the disable-freeze.
    initResidentCache(fresh);
    // Frozen (disabled) outputs keep their last result across the rebuild — TiXL slots persist,
    // ours are re-projected, so the freeze value must ride over (refuter-S2 P1×P7).
    transplantDisabledCaches(g_residentGraph, fresh);
    g_residentGraph = std::move(fresh);
    g_builtRev = doc::libRevision();
    // Rebuild the live-source downstream closure in lockstep with the graph projection.
    // Seeds from outCache.isLiveSource (set by initResidentCache above).
    rebuildLiveDownstreamClosure(g_residentGraph);
  }

  // Advance the two-clock transport by the REAL frame duration (S5) — RAW, no ceiling: the
  // transport is the master clock audio follows, so a stalled frame advances the playhead by the
  // full stall (TiXL Stopwatch semantics). Only the sim leg (ctx.deltaTime below) is clamped.
  // The BPM home is the lib's composition settings (saved/loaded) — pull it onto the transport
  // each frame so a load / inspector edit takes effect (cheap, no edge to chase).
  g_transport.bpm = doc::g_lib.composition.bpm;
  const double dtSecs = measureDeltaSeconds();        // true wall dt -> transport
  const double dtSimSecs = simDeltaFromWall(dtSecs);  // clamped copy  -> Metal sim only
  g_transport.advance(dtSecs);

  // Process-lifetime wall run clock = TiXL Playback.RunTimeInSecs analog (a Stopwatch.StartNew() at
  // static init, Playback.cs:159). simple_world has no such static clock, so we accumulate the SAME
  // raw wall dt the transport consumes — it advances at real wall-clock rate regardless of
  // pause/scrub/rate. Origin = 0 on the first cook (R-1 fork: StopWatch exposes only deltas, so the
  // baseline difference is invisible). Consumed by the stateful-value seam's StopWatch.
  static double s_runTimeSecs = 0.0;
  s_runTimeSecs += dtSecs;

  // Soundtrack follows the transport (TiXL: wall clock is master, audio chases — drift past
  // 0.04s hard-seeks, pause = stream pause, scrub-while-paused stays silent). The TARGET is the
  // PLAYHEAD in seconds: the soundtrack is timeline audio, frozen with the playhead on pause —
  // fxTime (the keeps-running brother) belongs to sims, never to the backing track. The rate
  // rides along so speed==0/negative/out-of-window pause the stream and in-window speeds reach
  // the varispeed (the refuter-C "rate=0+Playing = frozen playhead, music keeps running" trap).
  soundtrack::syncFrame(g_transport.playing(), g_transport.rate,
                        g_transport.secondsFromBars(g_transport.position));

  // Both clocks in BARS (P3, the resident eval ctx is bars-native): localTime = the PLAYHEAD
  // (automation samples this), localFxTime = the WALL CLOCK (stateful sims sample this; keeps
  // running while paused). AudioReaction eats fxTime in BARS — TiXL's LocalFxTime IS
  // Playback.FxTimeInBars (EvaluationContext.cs:49) and MinTimeBetweenHits compares in the same
  // domain; feeding seconds skews the debounce window at any BPM != 240 (refuter-S5 BROKEN-A).
  // ctx.time (the Metal sim uniform) stays in SECONDS — the pre-transport flat-sim contract the
  // shaders were tuned against; that is OUR unit fork, named, not TiXL's context.
  const double posBars = g_transport.position;
  const double fxBars = g_transport.fxTime;
  const double fxSecs = g_transport.secondsFromBars(fxBars);  // ctx.time only (sane-floored bpm)

  const SpectrumSnapshot spec = audio_monitor::spectrum();

  // Cook every AudioReaction instance — the seam itself (cookAudioReactionNodes below) derives
  // the AR clock from the transport, so the bars-domain decision has ONE home and the arclock
  // selftest exercises the very joint run() uses.
  {
    static std::map<std::string, AudioReactionState> s_arState;
    cookAudioReactionNodes(g_residentGraph, spec, g_transport, g_frameIndex, &doc::g_lib,
                           s_arState);
  }

  // Cook stateful value ops (Damp/Spring/...) right after AudioReaction — same once-per-frame slot,
  // same extOut-mirror contract. dtSecs is the RAW wall delta (these are CPU value sims, frame-rate
  // dependent like TiXL Playback.LastFrameDuration); each op clamps internally. State keys off the
  // resident path, so it rides projection rebuilds and stays per-instance inside compounds.
  // context-var seam: host per-frame var map (= TiXL EvaluationContext Float/IntVariables; cleared once/frame
  // in cookStatefulValueNodes). S3a: lifted OUT of the stateful block so the SAME instance reaches
  // pg.cookResident below — value-rail writers populate it, THEN the Command SetVarCmd push/restore augments it.
  static ContextVarMap s_ctxVars;
  {
    static std::map<std::string, StatefulValueState> s_svState;
    cookStatefulValueNodes(g_residentGraph, (float)dtSecs, (float)fxSecs, s_runTimeSecs, g_transport,
                           g_frameIndex, &doc::g_lib, s_svState, s_ctxVars);
  }

  // Cook the HOST-VALUE currencies (String / host-scalar / ColorList) — the PRODUCTION legs of the
  // resident string-wire rail, the list-routing FloatList→Float bridge, and the vec4-list cook flow.
  // Same once-per-frame slot as the AR / stateful cookers above (extOut-mirror contract). Extracted
  // into cookHostValueNodes (cook_host_values.cpp) so this file stays under its line-count cap while
  // the stateful-string seam threads its cross-frame state store; behaviour is byte-identical to the
  // old inline block (the cross-frame state statics moved in with it). Reads the two BARS clocks +
  // the lib (the flat cook*Node twins in point_graph.cpp are golden-only; this is the running app's leg).
  cookHostValueNodes(g_residentGraph, (float)posBars, (float)fxBars, &doc::g_lib);

  ++g_frameIndex;
  // The GPU EvaluationContext (16-byte) carries the WALL CLOCK (fxTime) as `time`: GPU-side Time/
  // particle sims are the fxTime consumers (暫停畫面不死). Automation (a CPU value-graph concern)
  // reads the playhead via the resident ctx above — it never reaches the shader through here.
  EvaluationContext ctx{};
  ctx.frameIndex = g_frameIndex;
  ctx.time = (float)fxSecs;          // wall clock, seconds (sims keep running while paused)
  ctx.deltaTime = (float)dtSimSecs;  // SIM dt: clamped (integration stability), NOT the wall dt
  // LocalFxTime seam (additive): the BARS wall clock (= TiXL EvaluationContext.LocalFxTime =
  // Playback.FxTimeInBars). ctx.time above is SECONDS for the GPU sims; value ops that need TiXL's
  // bars-domain LocalFxTime (PerlinNoise2) read this. Populates the offset-12 slot (was _pad); the
  // GPU upload never reads offset 12, so this is GPU-side a no-op (BI_EvalContext stays 16 bytes).
  ctx.localFxTime = (float)fxBars;

  // The resident cook fills ITS two-clock ctx (localTime/localFxTime, bars) from the transport
  // via these params, so automation-driven Float inputs sampled inside the cook walk the curve at
  // the playhead. (point_graph_resident reads them off the args — no more ctx.time placeholder.)
  // S3a: thread s_ctxVars (populated above) so a Command-rail SetVarCmd scopes a var around its SubGraph.
  pg.cookResident(g_residentGraph, ctx, /*reg=*/nullptr, targetPath,
                  (float)posBars, (float)fxBars, &doc::g_lib, &s_ctxVars);
  // editor-only: stamp lastUpdatePass on live nodes (Time/Automation-driven) so the UI's idle
  // fade signal is accurate. Static nodes keep their old lastUpdatePass (idle after 60 frames).
  stampLiveLastUpdatePass(g_residentGraph, g_frameIndex);
}

// --- Transport control surface (UI/selftest drive the playback head through these) ---
void transportPlay()  { g_transport.play(); }
void transportPause() { g_transport.pause(); }
void transportToggle() { g_transport.toggle(); }
void transportScrub(double bars) { g_transport.scrub(bars); }
bool transportPlaying() { return g_transport.playing(); }
double transportPosition() { return g_transport.position; }
double transportFxTime() { return g_transport.fxTime; }
double transportBpm() { return doc::g_lib.composition.bpm; }
void transportSetBpm(double bpm) {
  if (bpm < 1.0 || bpm > 999.0) return;   // sane range, same gate as the loader (tiny-positive
                                          // bpm makes secondsFromBars blow to inf — BROKEN-B)
  doc::g_lib.composition.bpm = bpm;       // the persistence home (saved in the v2 file)
  doc::invalidateDirtyCache();            // direct g_lib write bypasses bumpLibRevision (B4 fix)
  g_transport.bpm = bpm;
}
double transportRate() { return g_transport.rate; }
void transportSetRate(double rate) { g_transport.setRate(rate); }  // gate lives in the Transport
void transportPlayBackwards() { g_transport.playBackwards(); }

}  // namespace sw::framecook
