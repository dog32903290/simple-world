// ui/graph_dump — serialize the CURRENT compound into the JSON the agent's eye dumps as graph.json.
//
// WHY: the `connect`/`disconnect` hand verbs take childId + string slotId (資料層 id, 免座標), but an
// agent driving the canvas has no way to LEARN those ids without screenshotting. dumpgraph closes the
// loop: `touch req_graph` -> eye calls this hook -> graph.json lists every child (id, op type, ports
// with dataType/isInput/multiInput) + every wire (srcChild/srcSlot/dstChild/dstSlot) of the current
// compound, plus the breadcrumb. The agent reads ids from graph.json, then `connect`s — coordinate-free.
//
// LEAF INVERSION: verify/eye holds only a fn-ptr (setGraphDumpHook). This module (ui tier: reads
// runtime compound + NodeSpec types eye must not depend on) fills it. Mounted once at editor init
// (main.cpp), beside mountConnectionVerbs.
//
// ZONE: ui (reads doc current compound + runtime spec types) — same tier as connection_ops.
#pragma once
#include <string>

namespace sw::ui {

// Serialize doc::currentSymbol()'s subgraph to the graph.json schema (children/ports/connections +
// compound id/breadcrumb). Pure read of the live lib; no mutation. Returns a self-describing stub
// ({"compound": null, ...}) when there is no current compound. Exposed (not just the hook) so the
// headless self-test drives the SAME serializer the live req_graph path runs.
std::string serializeCurrentCompound();

// Install serializeCurrentCompound as verify/eye's graph-dump hook (leaf inversion). Call once at
// editor init (main.cpp), beside mountConnectionVerbs.
void mountGraphDump();

// Headless RED->GREEN proof (--selftest-graphdump): build a known compound, run the live
// req_graph path (eye::writeGraphDump via the mounted hook), parse graph.json back, and assert
// children/ports/connection counts + ids. injectBug drops a child from the build so the parsed
// dump is missing it -> the count assertion fires RED.
int runGraphDumpSelfTest(bool injectBug);

}  // namespace sw::ui
