// runtime/point_ops_logmessage — S3b LogMessage: a TRANSPARENT Command-rail SubGraph passthrough that
// fires a host-side log side-effect while forwarding the wrapped subtree's draw items unchanged. The
// cheapest S3b Execute-family op — no concat (single SubGraph), no selection (Switch), no re-cook (Loop):
// it cooks the one wired SubGraph (the driver's existing single-input Command collector already does this)
// and the op cook just (a) forwards the chain and (b) emits the Message to a log SINK.
//
// TiXL ground truth: flow/LogMessage.cs:20-58 (the Update):
//   var message = Message.GetValue(ctx);
//   if (onlyOnChanges && message == _lastMessage) return;   // de-dupe gate (:39-40)
//   _lastMessage = message;
//   if (logLevel > None) Log.Debug("<nesting> <message> @time <ms>", this);   // :47-50  THE log effect
//   SubGraph.GetValue(ctx);                                                    // :53     cook the wrapped subtree
// Load-bearing facts mirrored: (a) the SubGraph is cooked and its items pass through UNCHANGED (LogMessage is
// a wrapper, not a transform — :53 just evaluates the child for its draw side-effects); (b) the log fires
// only when logLevel > None (:48) AND, if OnlyOnChanges, only when the message text changed (:39-40); (c) the
// fallback "Log" string when Message is empty (:49 fallbackMessage). FORKS (named): the perf timing
// (_dampedPreviousUpdateDuration / Playback.RunTimeInSecs / UpdateTime level) + the global _nestingLevel
// indent are editor-telemetry embellishments dropped — sw has no Playback transport clock nor an editor log
// pane; the faithful, testable core is the gated message emission + the transparent SubGraph passthrough.
//
// ★COOK-CORE HOOK: NONE. LogMessage's SubGraph is a single (non-MultiInput) Command input → the driver's
// existing else-branch in cookCommand already cooks it into cc.inputCommand and breaks after wire 0. So this
// leaf adds ZERO cook-core change (unlike ExecRepeatedly's re-cook). The op cook reads cc.inputCommand,
// forwards it, and pushes the resolved Message into the log sink. The sink is a leaf-seam fn (a counter +
// last-message capture, zero upward dep) so the --selftest can assert the log fired without an editor pane.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"
#include "runtime/point_ops_logmessage.h"  // logMessageCurrentText (shared with the cook drivers)

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / cookParam / PointGraph
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── the log SINK (leaf seam — host log effect, zero upward dep) ─────────────────────────────
// LogMessage's only NON-passthrough effect is the Log.Debug line (LogMessage.cs:49). sw has no editor log
// pane, so the runtime exposes a tiny SINK an upper layer (app/ui) could later wire to a real console, and
// the --selftest reads to prove the gated emission fired. Mirrors how platform leaves expose fn-ptr data
// exits (ARCHITECTURE leaf-seam rule) — the runtime owns the data, the consumer is registered above.
struct LogSinkState {
  int count = 0;                 // how many times the log fired (the gate-passes counter)
  std::string lastMessage;       // the most recent emitted text (== TiXL _lastMessage after emit)
};
LogSinkState& logSink() { static LogSinkState s; return s; }

// LogMessage's Message text travels on the String channel. CmdCookCtx carries Float params only (it is not
// grown for this leaf — zero ABI churn) and its nodeId is NOT populated on the resident cook leg (cc.nodeId=0
// there), so the op cannot key its message by nodeId. Instead it resolves WHICH message to emit from a single
// process-scoped "current message", set by the caller before each cook. FORK (named, the one deferred wire):
// in production a LogMessage node's Message string would be threaded into the op by the cook driver from
// n->strParams / ResidentNode::strInputs the same way Loop/Switch read their String inputs; documented as
// deferred because no behaviour-bearing render path ships a LogMessage gate yet (it is an authoring/telemetry
// node, not a draw op). The passthrough + gate semantics ARE exercised on both legs; only the prod
// string-threading wire is deferred to when a real LogMessage authoring path lands.
std::string& logMessageCurrentText() { static std::string s = "Log"; return s; }
bool& logMessageSkipMessageThread() { static bool v = false; return v; }

// ───────────────────────────── the LogMessage op (transparent passthrough + gated emit) ─────────────────────────────
RenderCommand cookLogMessage(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;  // transparent passthrough (LogMessage.cs:53)

  const int logLevel = (int)(cookParam(c, "LogLevel", 1.0f) + 0.5f);     // .t3: None/Messages/UpdateTime; default Messages(1)
  const bool onlyOnChanges = cookParam(c, "OnlyOnChanges", 0.0f) > 0.5f;  // .t3 DefaultValue=false (de-dupe off)

  std::string message = logMessageCurrentText();
  if (message.empty()) message = "Log";  // fallback (LogMessage.cs:49 fallbackMessage)

  // Gate (LogMessage.cs:39-48): emit only when logLevel>None, and (if OnlyOnChanges) only when text changed.
  if (logLevel > 0) {
    if (!(onlyOnChanges && message == logSink().lastMessage)) {
      logSink().lastMessage = message;  // TiXL _lastMessage = message (:41)
      logSink().count++;
    }
  }
  return rc;
}

void registerLogMessageOp() { registerCmdOp("LogMessage", cookLogMessage); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-logmessage (S3b, BOTH legs). Topology: a stub Command op (one tagged draw item) → LogMessage
// .SubGraph → StubRenderTarget(terminal capture). Prove:
//   PASSTHROUGH: the captured chain carries the stub's item UNCHANGED (count tag 777 survives) — LogMessage
//     is transparent (LogMessage.cs:53).
//   GATED EMIT (closed-form): LogLevel=Messages(1), OnlyOnChanges=false → every cook fires → sink.count +1
//     per cook, lastMessage == the node's Message text.
//   DE-DUPE: OnlyOnChanges=true + SAME message → the next cook does NOT fire (count unchanged) — :39-40.
//   CHANGE: OnlyOnChanges=true + a DIFFERENT message → fires again — :39 (message != _lastMessage).
//   LEVEL-NONE: LogLevel=None(0) → no emit ever (count unchanged), passthrough still works — :48.
// -bug: cookLogMessageBug drops the gate (always emits) → the de-dupe + level-none assertions go RED. Both
//   legs (resident is the production leg — S2c blood lesson; a resident-only miss = a prod-only black-hole).
namespace {
RenderCommand g_capturedChain;

RenderCommand stubCmd(CmdCookCtx&) {
  RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 777u, 1.0f}); return rc;  // passthrough witness
}
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

// -bug variant: ALWAYS emit (drops both logLevel>None and OnlyOnChanges gates).
RenderCommand cookLogMessageBug(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  std::string message = logMessageCurrentText();
  if (message.empty()) message = "Log";
  logSink().lastMessage = message;
  logSink().count++;
  return rc;
}

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installLogMessageSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["StubCmd"] = atomicSpec("StubCmd", {{"out", "out", "Command", false}});
  dyn["LogMessage"] = atomicSpec("LogMessage",
      {{"SubGraph", "SubGraph", "Command", true},
       {"out", "out", "Command", false},
       {"OnlyOnChanges", "OnlyOnChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
       {"LogLevel", "LogLevel", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
        {"None", "Messages", "UpdateTime"}, true}});
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

// The LogMessage node's Message is ALWAYS carried on its strParams["Message"] (the production wire): the cook
// DRIVER threads it into logMessageCurrentText before the op cook. logMessageCurrentText is pre-seeded to a
// STALE marker so a working thread MUST overwrite it (a skipped thread → the op emits the stale text → bites).
bool cookLogGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                  bool onlyOnChanges, int logLevel, const std::string& msg, RenderCommand& outChain) {
  Graph g;
  Node st; st.id = 1; st.type = "StubCmd"; g.nodes.push_back(st);
  Node lm; lm.id = 2; lm.type = "LogMessage";
  lm.params["OnlyOnChanges"] = onlyOnChanges ? 1.0f : 0.0f;
  lm.params["LogLevel"] = (float)logLevel;
  lm.strParams["Message"] = msg;  // the production wire: driver threads strParams → logMessageCurrentText
  g.nodes.push_back(lm);
  Node rt; rt.id = 3; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // StubCmd.out → LogMessage.SubGraph
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // LogMessage.out → StubRenderTarget.command

  logMessageCurrentText() = "STALE-not-threaded";  // working thread must overwrite from strParams
  g_capturedChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/3);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*terminal path=*/"3");
  }
  outChain = g_capturedChain;
  return true;
}
}  // namespace

int runLogMessageSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-logmessage] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  if (injectBug) registerCmdOp("LogMessage", cookLogMessageBug);  // -bug variant: gate dropped
  else           registerLogMessageOp();                          // the REAL op under test
  logMessageSkipMessageThread() = injectBug;  // -bug: also skip the Message-thread → msgThread tooth bites
  registerCmdOp("StubCmd", stubCmd);
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installLogMessageSpecs();

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  for (int path = 0; path < 2; ++path) {
    logSink() = LogSinkState{};  // leg-local counts

    RenderCommand c1;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/false, /*logLevel=*/1, "A", c1);
    bool passthrough = (c1.items.size() == 1) && (c1.items[0].count == 777u);
    bool emit1 = (logSink().count == 1) && (logSink().lastMessage == "A");

    RenderCommand c2;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/false, /*logLevel=*/1, "A", c2);
    bool emit2 = (logSink().count == 2);  // OnlyOnChanges OFF → fires again on same msg

    RenderCommand c3;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/true, /*logLevel=*/1, "A", c3);
    bool dedupe = (logSink().count == 2);  // OnlyOnChanges ON + same msg → no fire

    RenderCommand c4;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/true, /*logLevel=*/1, "B", c4);
    bool changeFires = (logSink().count == 3);  // changed msg → fires

    RenderCommand c5;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/false, /*logLevel=*/0, "C", c5);
    bool levelNone = (logSink().count == 3) && (c5.items.size() == 1);  // None → no fire, still passthrough

    // MESSAGE-THREAD tooth (param-completion fan-out): cook with the LogMessage node's strParams["Message"]
    // = "hello" (the cookLogGraph helper pre-seeds logMessageCurrentText to a STALE marker). A working cook
    // driver threads strParams["Message"] → logMessageCurrentText → sink.lastMessage == "hello". The bug
    // (logMessageSkipMessageThread) skips the thread → the op emits the stale text → lastMessage != "hello".
    logSink() = LogSinkState{};  // isolate the message-thread count from the gate teeth above
    RenderCommand c6;
    cookLogGraph(dev, lib, q, path, /*onlyOnChanges=*/false, /*logLevel=*/1, /*msg=*/"hello", c6);
    bool msgThread = (logSink().lastMessage == "hello") && (logSink().count == 1);

    bool legOk = passthrough && emit1 && emit2 && dedupe && changeFires && levelNone && msgThread;
    allFaithful = allFaithful && legOk;
    std::printf("[selftest-logmessage] %s passthrough=%d emit1=%d emit2=%d dedupe=%d change=%d levelNone=%d "
                "msgThread=%d(last='%s') -> %s\n", pathName[path], passthrough, emit1, emit2, dedupe,
                changeFires, levelNone, msgThread, logSink().lastMessage.c_str(),
                legOk ? "faithful-ok" : "tripped");
  }

  logSink() = LogSinkState{};       // process hygiene
  logMessageCurrentText() = "Log";
  logMessageSkipMessageThread() = false;
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-logmessage] FAIL: injectBug still produced the faithful gate behaviour\n");
      return 1;
    }
    std::printf("[selftest-logmessage] injectBug correctly RED (gate dropped → de-dupe / level-none fired "
                "anyway → counts diverged on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-logmessage] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/328, {"logmessage", runLogMessageSelfTest});

}  // namespace sw
