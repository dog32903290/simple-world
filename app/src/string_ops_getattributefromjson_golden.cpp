// string_ops_getattributefromjson_golden — GetAttributeFromJsonString golden, a NEW standalone golden file
// (kept ≤400 lines, new-file-safe) that self-registers as --selftest-getattributefromjson (+ -bug) via
// REGISTER_SELFTESTS. It does NOT edit the shared string_rail_golden.cpp (no shared-file edit → parallel-
// merge safe). This is a [G] HERMETIC golden: the JSON is a STRING LITERAL in the test (no fixture file).
//
// TiXL authority: Operators/Lib/io/json/GetAttributeFromJsonString.cs. Outputs Result(String, port 0),
// Columns(List<string> → here a ", "-joined String, fork-jsonattr-columns-joined-string, port 1),
// RowCount(Int→Float, port 2). Inputs JsonString(String), ColumnName(String), RowIndex(Int).
//
// fork-jsonattr-flat-only-no-resident: this op rides ONLY the flat String cook flow (it does not enter the
// resident eval graph, like every multi-output String op). The golden asserts the FLAT leg only — named.
//
// GREEN legs (every cell value distinct so a collapse can't pass by coincidence):
//   JSON = [{"name":"red","r":"255"},{"name":"green","r":"0"}]
//   (A) ColumnName="name", RowIndex=1 → Result="green"
//   (B) ColumnName="r",    RowIndex=0 → Result="255"   (number stringified verbatim, here a JSON string)
//   (C) RowCount = 2 ; Columns = "name, r" (union of keys, source order)
//   (D) out-of-range row (RowIndex=5) → Result="" (fork-jsonattr-rowindex-off-by-one-faithful)
//   (D') boundary row (RowIndex=2 == rowCount) → Result="" (the TiXL strict-`<` off-by-one is unreachable)
//   (E) missing column (ColumnName="nope") → Result="" (Columns/RowCount still emitted)
//   (F) number value stringification: JSON=[{"k":42}], ColumnName="k", RowIndex=0 → Result="42"
//       (bare JSON number, echoed verbatim — proves number→string without quotes in the source)
//
// BUG leg (-bug): cookGetAttributeFromJson reads the cell at rowIndex+1 (off-by-one row), drops the last
// char of the joined Columns, and sets RowCount=-999 — a REAL perturbation of the parse/index path (NOT a
// want-flip). Under -bug case (A) Result reads row 2 (out of range) → "" ≠ "green" → FAIL (exit 1).
#include <cmath>
#include <cstdio>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"          // EvaluationContext
#include "runtime/graph.h"                 // Graph/Node/pinId/outCache
#include "runtime/point_graph.h"           // PointGraph::cook + debugCookedString(Port)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"   // stringInjectBug

namespace sw {

namespace {

// Cook one GetAttributeFromJsonString node (flat) and read back Result(port0) / Columns(port1, joined
// String) / RowCount(port2, flat scalar via outCache). json/columnName are unwired strDef consts.
struct JsonAttrResult {
  std::string result;
  std::string columns;
  float rowCount = -1.0f;
};

JsonAttrResult cookJsonAttr(PointGraph& pg, const std::string& json, const std::string& columnName,
                            int rowIndex) {
  Graph g;
  Node n;
  n.id = 1;
  n.type = "GetAttributeFromJsonString";
  n.strParams["JsonString"] = json;        // unwired → strDef const carries the JSON
  n.strParams["ColumnName"] = columnName;   // unwired → strDef const carries the column name
  n.params["RowIndex"] = static_cast<float>(rowIndex);
  g.nodes.push_back(n);

  EvaluationContext ctx{};
  ctx.frameIndex = 0;
  ctx.time = 0.0f;
  ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  JsonAttrResult r;
  const std::string* res = pg.debugCookedString(1);          // Result (port 0)
  const std::string* col = pg.debugCookedStringPort(1, 1);   // Columns (port 1, joined string)
  r.result = res ? *res : std::string{};
  r.columns = col ? *col : std::string{};
  if (const Node* gn = g.node(1)) r.rowCount = gn->outCache[2];  // RowCount (port 2, flat scalar bridge)
  return r;
}

int runJsonAttrSelftestImpl(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  const std::string json = R"([{"name":"red","r":"255"},{"name":"green","r":"0"}])";

  stringInjectBug() = injectBug;
  JsonAttrResult a = cookJsonAttr(pg, json, "name", 1);  // (A) → "green"
  JsonAttrResult b = cookJsonAttr(pg, json, "r", 0);     // (B) → "255"
  JsonAttrResult d = cookJsonAttr(pg, json, "name", 5);  // (D) out-of-range row → ""
  JsonAttrResult dp = cookJsonAttr(pg, json, "name", 2); // (D') boundary row==rowCount → ""
  JsonAttrResult e = cookJsonAttr(pg, json, "nope", 0);  // (E) missing column → ""
  JsonAttrResult f = cookJsonAttr(pg, R"([{"k":42}])", "k", 0);  // (F) bare number → "42"
  stringInjectBug() = false;

  // (A)+(B): distinct cell values, distinct columns.
  bool aOk = (a.result == "green");
  bool bOk = (b.result == "255");
  // (C): RowCount=2, Columns="name, r" (union of keys in source order, ", "-joined fork).
  bool countOk = (std::fabs(a.rowCount - 2.0f) < 1e-5f) && (std::fabs(b.rowCount - 2.0f) < 1e-5f);
  bool colsOk = (a.columns == "name, r") && (b.columns == "name, r");
  // (D)+(D'): out-of-range / boundary row → empty Result (Columns/RowCount still emitted).
  bool dOk = (d.result == "") && (std::fabs(d.rowCount - 2.0f) < 1e-5f) && (d.columns == "name, r");
  bool dpOk = (dp.result == "") && (std::fabs(dp.rowCount - 2.0f) < 1e-5f);
  // (E): missing column → empty Result, but Columns/RowCount still present.
  bool eOk = (e.result == "") && (std::fabs(e.rowCount - 2.0f) < 1e-5f) && (e.columns == "name, r");
  // (F): bare JSON number stringified verbatim → "42", single column "k", one row.
  bool fOk = (f.result == "42") && (f.columns == "k") && (std::fabs(f.rowCount - 1.0f) < 1e-5f);
  // Cross-distinctness: no collapse where everything is the same string.
  bool distinct = (a.result != b.result) && (a.result != "") && (b.result != "");

  bool pass = aOk && bOk && countOk && colsOk && dOk && dpOk && eOk && fOk && distinct;
  ok = ok && pass;

  std::printf("[selftest-getattributefromjson] "
              "(A name@1)=\"%s\" want=\"green\"; (B r@0)=\"%s\" want=\"255\"; "
              "RowCount=%.1f want=2.0; Columns=\"%s\" want=\"name, r\"; "
              "(D row5)=\"%s\" want=\"\"; (D' row2==count)=\"%s\" want=\"\"; "
              "(E col=nope)=\"%s\" want=\"\"; (F bare-num)=\"%s\" want=\"42\" -> %s\n",
              a.result.c_str(), b.result.c_str(), a.rowCount, a.columns.c_str(),
              d.result.c_str(), dp.result.c_str(), e.result.c_str(), f.result.c_str(),
              pass ? "PASS" : "FAIL");

  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-getattributefromjson] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-getattributefromjson (+ -bug) for isolated runs. orderBase 360 is free
// (above the string-op cluster, below the high reserved 500/600 rows).
REGISTER_SELFTESTS(/*orderBase=*/360, {"getattributefromjson", runJsonAttrSelftestImpl});

}  // namespace sw
