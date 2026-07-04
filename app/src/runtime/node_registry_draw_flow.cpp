// runtime/node_registry_draw_flow — NodeSpec rows for the FLOW family: the flow / flow.context Command-rail
// control ops (Execute/Loop/ExecuteOnce/ExecRepeatedly/LogMessage + the SetXxxVarCmd context-var writers).
// Peeled out of node_registry_draw.cpp so the flow lane can extend this family without touching drawSpecs()'s
// shared table (parallel-lane peel — no merge conflict with render/camera/data lanes). These rows moved
// VERBATIM from node_registry_draw.cpp; table order unchanged (drawSpecs() appends in source order).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawFlowSpecs() {
  static const std::vector<NodeSpec> specs = {
      // SetFloatVarCmd (TiXL Lib.flow.context.SetFloatVar, the SubGraph branch :26-45): the Command-rail
      // twin of the value-rail SetFloatVar — wraps a Command SubGraph and, while cooking it, pushes
      // FloatValue into context.FloatVariables[VariableName], restoring after (hadPrev ? prev : ClearAfter ?
      // keep : Remove). The PUSH/RESTORE lives in the cook driver (cookCommand S3a branch); the op cook just
      // forwards the subtree's items (like SetRequestedResolution). VariableName is a String input (strDef
      // "f", the float-only value rail carries it on the String channel). NAMED FORK: TiXL's ONE dual-branch
      // node becomes two sw types — the no-SubGraph float write stays the value-rail "SetFloatVar"; this is
      // the SubGraph (Command) half. Command in → Command out. .t3: FloatValue=0, ClearAfterExecution=false.
      {"SetFloatVarCmd", "SetFloatVar",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "f"},
        {"FloatValue", "FloatValue", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow.context"},
      // SetIntVarCmd (TiXL Lib.flow.context.SetIntVar, the SubGraph branch :38-64): the int twin of
      // SetFloatVarCmd. Value arrives on a Float port (no Int port type) → truncated toward zero (C# (int)
      // cast convention) and pushed into context.IntVariables[VariableName] around the SubGraph. strDef "i".
      // Command in → Command out. .t3: Value=0, ClearAfterExecution=false.
      {"SetIntVarCmd", "SetIntVar",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow.context"},
      // SetBoolVarCmd (TiXL Lib.flow.context.SetBoolVar, the SubGraph branch :25-36): the bool twin of
      // SetFloatVarCmd. BoolValue arrives on a Float port (no Bool port type) → !=0 ⇒ 1 and pushed into the
      // INT channel (context.IntVariables[VariableName]) around the SubGraph. NAMED FORK: sw has no boolVars
      // dict, so bool rides intVars as 0/1. strDef "b". Command in → Command out. .t3: BoolValue=false,
      // ClearAfterExecution=false. The PUSH/RESTORE is the generic cmdVarPush isBool branch (driver-shared).
      {"SetBoolVarCmd", "SetBoolVar",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "b"},
        {"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow.context"},
      // Execute (TiXL Lib.flow.Execute): the S2a KEYSTONE — a MULTIINPUT Command port that concatenates
      // N wired Command chains in wire-declaration order into ONE chain (Execute.cs CollectedInputs). The
      // cook-core collector (cookCommand's MultiInput Command branch) does the gather+concat; this op just
      // gates on IsEnabled. Command(MultiInput) in → Command out. This is what lets the ~155 Slot<Command>
      // render ops compose (every render op outputs Command and they only chain through a MultiInput Group/
      // Execute). The Command port carries multiInput=true (the {..., false, 1, true} positional tail = the
      // FloatsToList/Values precedent); no Float fields are meaningful on it (placeholders). IsEnabled is a
      // Widget::Bool (.t3 DefaultValue = true). FORK (named): VisibleGizmos = Execute without transform =
      // the same op; Group (Execute + SRT push) is S2b, out of scope here.
      {"Execute", "Execute",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow"},
      // Loop (TiXL Lib.flow.Loop): the S3c RE-COOK keystone — cooks the wired SubGraph Count times, each
      // iteration writing index→Float+Int and progress→Float context-vars first (Loop.cs:25-35), concatenating
      // every iteration's items. The per-iteration var write + live-scope + re-cook + concat lives in the
      // cook-core collector (cookCommand's Loop branch → loopRunIterations), shared flat+resident; this op just
      // forwards the built chain. Faithful no-restore after the loop (Loop.cs:21 TODO leaks index/progress).
      // Command(SubGraph) in → Command out. IndexVariable/ProgressVariable on the String channel (strDef
      // "Index"/"Progress" — TiXL's input slots have no default name). .t3: Count=0.
      {"Loop", "Loop",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"Count", "Count", "Float", true, 0.0f, 0.0f, 1000.0f},
        {"IndexVariable", "IndexVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "Index"},
        {"ProgressVariable", "ProgressVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "Progress"}},
       nullptr,
       "flow"},
      // ExecuteOnce (TiXL Lib.flow.ExecuteOnce): the GATED Execute — a MultiInput Command port that
      // concatenates N wired chains in wire order (== Execute, the S2a collector) only when Trigger is set;
      // not triggered ⇒ empty (no draws). The driver's MultiInput Command collector does the gather+concat
      // (zero cook-core change); the op cook applies the Trigger gate (like Execute applies IsEnabled).
      // Command(MultiInput) in → Command out. NAMED BEHAVIORAL FORK: TiXL gates on
      // Trigger.DirtyFlag.IsDirty (a per-frame self-clearing edge latch → execute exactly once per trigger
      // edge; a held-true Trigger fires once-ever in TiXL, every-frame in sw); sw models it as the Trigger
      // VALUE (>0.5 ⇒ execute, ≤0.5 ⇒ skip) — cross-frame edge-latch deferred (needs frame-state). This
      // is NOT a faithful deferral like SkipFrameCount: TiXL has no value that disables once-ness.
      // OutputTrigger bool output dropped (no bool Command-side port; editor wiring not a draw effect).
      // .t3: Trigger DefaultValue=false.
      {"ExecuteOnce", "ExecuteOnce",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow"},
      // LogMessage (TiXL Lib.flow.LogMessage): a TRANSPARENT Command-rail SubGraph passthrough that fires a
      // host log side-effect while forwarding the wrapped subtree's draw items unchanged (LogMessage.cs:53).
      // The single (non-MultiInput) SubGraph is cooked by the driver's existing collector (zero cook-core
      // change); the op cook forwards the chain + emits the Message to a log sink when logLevel>None and (if
      // OnlyOnChanges) the text changed (LogMessage.cs:39-48). Command(SubGraph) in → Command out. FORKS
      // (named): perf timing (_dampedPreviousUpdateDuration / Playback.RunTimeInSecs / UpdateTime level) +
      // _nestingLevel indent dropped (no Playback clock / editor pane); Message string is on the String
      // channel, resolved by the op via a process-scoped per-node map — the one deferred prod string-thread
      // wire (no behaviour-bearing render path ships a LogMessage gate; authoring/telemetry node). .t3:
      // OnlyOnChanges=false, LogLevel default Messages(1).
      // Message (param-completion fan-out, LogMessage.cs:66-67 InputSlot<string>, .t3 default ""): emitted text
      // (empty → "Log", cs:54); the cook driver threads its strParams/strInputs into logMessageCurrentText().
      {"LogMessage", "LogMessage",
       {{"SubGraph", "SubGraph", "Command", true},
        {"out", "out", "Command", false},
        {"OnlyOnChanges", "OnlyOnChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Message", "Message", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"LogLevel", "LogLevel", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "Messages", "UpdateTime"}, true}},
       nullptr,
       "flow"},
      // ExecRepeatedly (TiXL Lib.flow.ExecRepeatedly): the Loop SIBLING — a MultiInput Command port whose
      // wired subtrees RE-EXECUTE `RepeatCount` (clamped [0,100], :24) times, concatenating each repetition,
      // with NO context-var injection (unlike Loop's index/progress). The per-repetition re-cook + concat
      // lives in the cook-core collector (cookCommand's ExecRepeatedly branch → execRepeatedlyRunRepetitions),
      // shared flat+resident; this op just forwards the built chain. Command(MultiInput) in → Command out.
      // FORK (named): SkipFrameCount + _callsSinceLastRefresh (:27-34) are a per-frame skip-throttle counter;
      // sw ships the SkipFrameCount=0 .t3 default (execute every call) — the frame-skip throttle is the
      // deferred frame-state half (same class as ExecuteOnce's DirtyFlag latch). .t3: RepeatCount=1,
      // SkipFrameCount=0.
      {"ExecRepeatedly", "ExecRepeatedly",
       {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"RepeatCount", "RepeatCount", "Float", true, 1.0f, 0.0f, 100.0f},
        {"SkipFrameCount", "SkipFrameCount", "Float", true, 0.0f, 0.0f, 10000.0f}},
       nullptr,
       "flow"},
  };
  return specs;
}

}  // namespace sw
