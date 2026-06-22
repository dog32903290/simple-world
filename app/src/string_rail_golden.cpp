// string_rail_golden — --selftest-stringrail. TRANSPORT + GATHER-ORDER + INPUT-DRIVABLE golden for
// the 6th cook flow (host std::string): the String value rail. Mirror of floatlist_golden.cpp.
//
// Build flat graphs that exercise the three new String leaves end-to-end through PointGraph::cook
// (the cookStringNode + cookStringLength branches), read the host result back (debugCookedString for
// String producers, debugCookedFloatList for StringLength's host scalar), and assert against
// closed-form values hand-derived from the TiXL .cs sources (NOT self-consistent — every expected
// value is computed by hand here).
//
// What this proves (the architecture-defining parts of the seam):
//   (1) TRANSPORT: a String producer's output flows node→node via the driver-owned stringBuf
//       (FloatToString → readback "3.14"; FloatToString → CombineStrings → readback joined).
//   (2) GATHER ORDER: CombineStrings' MultiInput honours WIRE-DECLARATION order (the load-bearing
//       spot, mirroring floatlist) — wires [a,b,c] → "a-b-c", not sorted, not first-wire-only.
//   (3) INPUT-DRIVABLE (the new fork): a String input port is WIRE-OR-CONST — StringLength reads the
//       wired upstream string when wired, and the strDef const when the wire is cut.
//   (4) FORMAT (now NON-DEGENERATE — earlier LEGs 1-10 only hit the plain-decimal band / n≤3 digits /
//       1e6 / NaN, so they閃過 the three real divergence points the refuter flagged):
//       • default ToString of moderate values ("{0:0.000}" 3 digits, "{0:F1}", 1000000 → "1000000");
//       • LEG 11 (BLOCK-2) large-magnitude default → SCIENTIFIC at exp≥7: 1e11 → "1E+11", 1e14 →
//         "1E+14" (uppercase E, sign, min-2-digit exponent — NOT noise integers, NOT 1e15-wide band);
//       • LEG 12 (BLOCK-3) small-magnitude default → SCIENTIFIC at exp<-4: 1e-5 → "1E-05", 1.234e-5 →
//         "1.234E-05" (uppercase E, NOT %g's lowercase "1e-05");
//       • LEG 13 (BLOCK-1) high-precision F/N/E specifiers EXPOSE the float→double binary value
//         (TiXL net10.0 / .NET Core 3.0+): "{0:F8}" 3.14 → "3.14000010", "{0:E10}" 0.1 →
//         "1.0000000149E-001" — NOISE, not zero-padded (the work-order's zero-pad values were .NET
//         Framework 4.x behaviour; corrected to net10.0 ground-truth here);
//       • NaN → "NaN" (C# literal, not printf "nan").
//
// injectBug routes through stringInjectBug(): the RED case corrupts the REAL cook output (drops the
// last char / clears StringLength's host scalar), NOT the expected value — teeth on the actual op
// path (mirror of floatlist_golden.cpp / mesh_golden.cpp).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"      // SymbolLibrary (R-2 resident leg)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph::cook + debugCookedString/debugCookedFloatList
#include "runtime/host_scalar_op_registry.h"  // hostScalarInjectBug (LEG 25 IndexOf teeth)
#include "runtime/resident_eval_graph.h" // buildEvalGraph / cookStringNodes / ResidentEvalGraph (R-2)
#include "runtime/string_op_registry.h"  // stringInjectBug

namespace sw {

// Sub-seam A legs (LEG 34/35/36) live in string_rail_golden_subseama.cpp — run under this same flag.
bool runStringRailSubseamA(bool injectBug);
// LEG 37/38 (StringBuilder + StringBuilderToString) live in string_builder_golden.cpp — same flag.
bool runStringBuilderGolden(bool injectBug);

namespace {

// Build a graph with a single FloatToString(value, format) and cook it as the terminal. Returns the
// cooked host string (empty on failure). format is set on strParams["Format"] (the stored String const).
std::string cookFloatToString(PointGraph& pg, float value, const std::string& format) {
  Graph g;
  Node ftl; ftl.id = 1; ftl.type = "FloatToString";
  ftl.params["Value"] = value;          // Value (resolved Float param, the value spine)
  ftl.strParams["Format"] = format;     // Format (String const — wire-OR-const, here the const path)
  g.nodes.push_back(ftl);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// Build { producers (StringConst-style via FloatToString of integer values) -> CombineStrings.Input
// (multiInput), in vals order, sep } — the SHARED graph driven by BOTH the flat and resident legs. Each
// producer is a FloatToString with empty format → its integer value's decimal text. Wire order = vals
// order → wire-declaration order; the readback must equal the joined sequence IF the gather honours it.
// CombineStrings is node id 1 (→ resident path "1"); producers are ids 2.. (FloatToString.Output →
// CombineStrings.Input is a GENUINE String wire — the whole point of the resident string-wire rail).
Graph makeCombineGraph(const std::vector<float>& producerVals, const std::string& sep) {
  Graph g;
  Node cmb; cmb.id = 1; cmb.type = "CombineStrings";
  cmb.strParams["Separator"] = sep;  // Separator (String const)
  g.nodes.push_back(cmb);
  // CombineStrings ports: [0]=Result(out), [1]=Input(multiInput), [2]=Separator. Input pin = port 1.
  const int inputPin = pinId(1, /*portIndex=*/1);

  int connId = 100;
  int nextNode = 2;
  for (size_t i = 0; i < producerVals.size(); ++i) {
    Node p; p.id = nextNode++; p.type = "FloatToString";
    p.params["Value"] = producerVals[i];
    p.strParams["Format"] = "";  // empty → default ToString (integer-valued → "1","2",...)
    g.nodes.push_back(p);
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format. Output pin = port 0.
    g.connections.push_back({connId++, pinId(p.id, /*out port*/ 0), inputPin});
  }
  g.nextId = nextNode;  // monotonic floor for libFromGraph (resident leg)
  return g;
}

// FLAT leg: cook CombineStrings as the terminal, read back via debugCookedString.
std::string cookCombine(PointGraph& pg, const std::vector<float>& producerVals,
                        const std::string& sep) {
  Graph g = makeCombineGraph(producerVals, sep);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// ★R-2 RESIDENT leg (production): mirror the SAME flat Graph into a SymbolLibrary (libFromGraph →
// resident paths == flat node ids as strings), build the resident graph, run cookStringNodes (the
// per-frame production pass frame_cook.cpp drives — String producers gathered THROUGH the resident
// String-wire drivers the flatten now projects), then read the host string off CombineStrings'
// extStrOut[Result port idx 0] — the EXACT production channel a downstream resident consumer reads.
// This is the leg that was a DROPPED wire before the rail: without it, CombineStrings.Input read its
// unwired-const fallback (empty) on the resident path → joined "" — the flat-only self-deception.
std::string cookCombineResident(const std::vector<float>& producerVals, const std::string& sep) {
  Graph g = makeCombineGraph(producerVals, sep);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookStringNodes(rg, rc);  // PRODUCTION pass: walks the resident graph, writes extStrOut
  const ResidentNode* n = rg.node("1");  // CombineStrings resident path == flat node id "1"
  if (!n) return {};
  auto it = n->extStrOut.find(/*Result out port idx*/ 0);
  return it != n->extStrOut.end() ? it->second : std::string{};
}

// Cook StringLength with its InputString either WIRED to a FloatToString producer (whose text we
// know exactly: FloatToString(3.14,"") → "3.14", 4 chars) or UNWIRED so it falls back to the strDef
// const (constText, set via strParams). Returns the host scalar (the character count) via
// debugCookedFloatList[0], or -1 on failure. The only String SOURCE leaf in this batch is
// FloatToString, so the wired leg's literal comes from it (CombineStrings could chain but adds no
// coverage here — FloatToString already proves the wired-recurse path).
float cookStringLength(PointGraph& pg, bool wired, const std::string& constText) {
  Graph g;
  Node sl; sl.id = 1; sl.type = "StringLength";
  if (!wired) sl.strParams["InputString"] = constText;  // unwired → strDef const path
  g.nodes.push_back(sl);
  // StringLength ports: [0]=Length(out), [1]=InputString. InputString pin = port 1.
  const int inPin = pinId(1, /*portIndex=*/1);

  if (wired) {
    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14" (4 chars)
    g.nodes.push_back(fts);
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format. Output pin = port 0.
    g.connections.push_back({300, pinId(2, /*out*/ 0), inPin});  // FloatToString.Output → InputString
  }

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);

  const std::vector<float>* out = pg.debugCookedFloatList(1);
  return (out && !out->empty()) ? (*out)[0] : -1.0f;
}

// Build a single IntToString(value, format) and cook it as terminal → cooked host string. Value rides
// params["Value"] (the value spine, truncated to int by the op); format rides strParams["Format"].
std::string cookIntToString(PointGraph& pg, int value, const std::string& format) {
  Graph g;
  Node n; n.id = 1; n.type = "IntToString";
  n.params["Value"] = (float)value;
  n.strParams["Format"] = format;
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// Build a single Vec3ToString(vector, format) and cook it as terminal → cooked host string. The vec3
// rides params["Vector.x"/.y/.z"] (the value spine); format rides strParams["Format"].
std::string cookVec3ToString(PointGraph& pg, float x, float y, float z, const std::string& format) {
  Graph g;
  Node n; n.id = 1; n.type = "Vec3ToString";
  n.params["Vector.x"] = x; n.params["Vector.y"] = y; n.params["Vector.z"] = z;
  n.strParams["Format"] = format;
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// Build a single ChangeCase(inputText, mode) flat graph and cook it as the terminal → host string.
// inputText rides strParams["InputText"] (the strDef const — single String input, unwired here for
// the flat leg). Mode rides params["Mode"] (resolved Float param, the value spine; 0=ToUpperCase,
// 1=ToLowerCase). Returns the cooked host string (empty on failure).
std::string cookChangeCaseFlat(PointGraph& pg, const std::string& inputText, int mode) {
  Graph g;
  Node n; n.id = 1; n.type = "ChangeCase";
  n.strParams["InputText"] = inputText;       // String const (unwired → strDef path)
  n.params["Mode"]         = (float)mode;     // enum stored as Float (int-on-Float contract)
  g.nodes.push_back(n);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::string* out = pg.debugCookedString(1);
  return out ? *out : std::string{};
}

// ★R-2 RESIDENT leg for ChangeCase (mirror of LEG 20 CombineStrings resident leg): wire a
// FloatToString producer (→ "Hello World" via format tricks... actually use a StringConst-style
// approach: wire a FloatToString(0,"") → "0" then use CombineStrings, but simpler: just test
// ChangeCase directly wired to a FloatToString whose output we know). Here we test ChangeCase
// wired to a FloatToString producer that yields "Hello World" — but FloatToString can't emit
// arbitrary text; use the const (unwired) path for one sub-case + wire a FloatToString for the
// other (proving the resident String-wire path is live for ChangeCase's InputText port).
//
// Sub-case A (const): ChangeCase(strDef="hello world", ToUpper) via cookStringNodes alone —
// the InputText is strDef const, no wire, so the resident flatten supplies the strDef.
// Sub-case B (wired): FloatToString(42,"") → "42" wired into ChangeCase.InputText → ToLower
// → "42" (digits are unaffected by case). This proves the resident String-wire path is live.
//
// Graph layout: ChangeCase = id 1 (terminal); FloatToString = id 2 (producer, sub-case B only).
// ChangeCase ports: [0]=Result(out), [1]=InputText(String input), [2]=Mode(Float). InputText pin = port 1.
// FloatToString ports: [0]=Output(out). Output pin = port 0.
std::string cookChangeCaseResident(bool wiredInput, const std::string& constText, int mode) {
  Graph g;
  Node cc; cc.id = 1; cc.type = "ChangeCase";
  cc.params["Mode"] = (float)mode;
  if (!wiredInput) cc.strParams["InputText"] = constText;  // unwired → strDef const
  g.nodes.push_back(cc);

  if (wiredInput) {
    // Wire FloatToString(42,"") → ChangeCase.InputText. "42" lowercased → "42" (digits unchanged;
    // this proves the wire is live, not that the transform changes anything non-trivial).
    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 42.0f; fts.strParams["Format"] = "";  // → "42"
    g.nodes.push_back(fts);
    // ChangeCase.InputText pin = port 1; FloatToString.Output pin = port 0.
    g.connections.push_back({200, pinId(2, /*out*/ 0), pinId(1, /*InputText*/ 1)});
    g.nextId = 3;
  } else {
    g.nextId = 2;
  }

  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  cookStringNodes(rg, rc);  // PRODUCTION pass: cooks producers + ChangeCase → extStrOut
  const ResidentNode* nd = rg.node("1");  // ChangeCase resident path == flat node id "1"
  if (!nd) return {};
  auto it = nd->extStrOut.find(/*Result out port idx*/ 0);
  return it != nd->extStrOut.end() ? it->second : std::string{};
}

}  // namespace

int runStringRailSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // LEG 1 — FloatToString TRANSPORT + default-format: FloatToString(3.14, "") → "3.14". Proves a
  // String producer's output is readable via the driver-owned stringBuf (the transport mechanism).
  // injectBug drops the last char in the REAL cook → "3.1" ≠ "3.14" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookFloatToString(pg, 3.14f, "");
    stringInjectBug() = false;
    bool pass = (got == "3.14");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"\")=\"%s\" want=\"3.14\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 2 — FloatToString FORMAT "{0:F1}": FloatToString(3.14, "{0:F1}") → "3.1" (fixed 1 digit).
  // injectBug drops the last char → "3." ≠ "3.1" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookFloatToString(pg, 3.14f, "{0:F1}");
    stringInjectBug() = false;
    bool pass = (got == "3.1");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"{0:F1}\")=\"%s\" want=\"3.1\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 3 — CombineStrings GATHER ORDER (the subtle spot): producers of [1,2,3] wired in order, sep="-"
  // → "1-2-3" (wire-declaration order, NOT sorted, NOT first-wire-only). Each producer is a
  // FloatToString of an integer value (empty format → "1"/"2"/"3"). injectBug (drop last char) →
  // "1-2-" ≠ "1-2-3" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookCombine(pg, {1.0f, 2.0f, 3.0f}, "-");
    stringInjectBug() = false;
    bool pass = (got == "1-2-3");
    ok = ok && pass;
    std::printf("[selftest-stringrail] CombineStrings([1,2,3],\"-\")=\"%s\" want=\"1-2-3\" -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 4 — CombineStrings wire-order proof: producers [3,1,2] must read back "3-1-2" (wire-declaration
  // order), NOT sorted "1-2-3". This is the load-bearing gather assertion (mirror of floatlist LEG 2).
  // injectBug (drop last char) → "3-1-" ≠ "3-1-2" → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookCombine(pg, {3.0f, 1.0f, 2.0f}, "-");
    stringInjectBug() = false;
    bool pass = (got == "3-1-2");
    ok = ok && pass;
    std::printf("[selftest-stringrail] CombineStrings([3,1,2],\"-\")=\"%s\" want=\"3-1-2\" (wire-decl) -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 5 — StringLength INPUT-DRIVABLE, WIRED: StringLength ← FloatToString(3.14,"") = "3.14" (4
  // chars) → host scalar 4.0. Proves a String input port reads the WIRED upstream string. injectBug
  // clears the REAL host scalar → empty (read -1) ≠ 4.0 → RED.
  {
    stringInjectBug() = injectBug;
    float got = cookStringLength(pg, /*wired=*/true, "");
    stringInjectBug() = false;
    bool pass = std::fabs(got - 4.0f) < 1e-5f;
    ok = ok && pass;
    std::printf("[selftest-stringrail] StringLength(wired \"3.14\")=%.1f want=4.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 6 — StringLength INPUT-DRIVABLE, UNWIRED const fallback: cut the wire → StringLength reads its
  // strDef const "Line\nLine" (9 chars: L,i,n,e,\n,L,i,n,e) → host scalar 9.0. Proves the wire-OR-const
  // dual identity (the new fork) — both the wired path (LEG 5) and the const path are live. injectBug
  // clears the host scalar → -1 ≠ 9.0 → RED.
  {
    stringInjectBug() = injectBug;
    float got = cookStringLength(pg, /*wired=*/false, "Line\nLine");  // real newline → 9 chars
    stringInjectBug() = false;
    bool pass = std::fabs(got - 9.0f) < 1e-5f;
    ok = ok && pass;
    std::printf("[selftest-stringrail] StringLength(const \"Line\\nLine\")=%.1f want=9.0 -> %s\n",
                got, pass ? "PASS" : "FAIL");
  }

  // LEG 7 — FloatToString DEFAULT FORMAT "{0:0.000}" (the TiXL .t3:6 default — the load-bearing case
  // LEGs 1-2 閃過 because they used "" / "{0:F1}"). C# custom numeric "0.000" = 3 fixed fractional
  // digits → v=0 → "0.000", v=3.14 → "3.140". Proves the default-format path matches TiXL's
  // string.Format(InvariantCulture, "{0:0.000}", v). injectBug drops last char → "0.00" ≠ "0.000" RED.
  {
    stringInjectBug() = injectBug;
    std::string got0 = cookFloatToString(pg, 0.0f, "{0:0.000}");
    std::string got314 = cookFloatToString(pg, 3.14f, "{0:0.000}");
    stringInjectBug() = false;
    bool pass = (got0 == "0.000") && (got314 == "3.140");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(0,\"{0:0.000}\")=\"%s\" want=\"0.000\"; "
                "(3.14)=\"%s\" want=\"3.140\" -> %s\n",
                got0.c_str(), got314.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 8 — FloatToString COMPOSITE LITERAL: surrounding text kept verbatim, placeholder substituted.
  // string.Format(InvariantCulture, "x={0:F1}", 3.14) → "x=3.1"; "{0:F1} units" → "3.1 units".
  // injectBug drops last char → "x=3." / "3.1 unit" ≠ expected → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotPre = cookFloatToString(pg, 3.14f, "x={0:F1}");
    std::string gotPost = cookFloatToString(pg, 3.14f, "{0:F1} units");
    stringInjectBug() = false;
    bool pass = (gotPre == "x=3.1") && (gotPost == "3.1 units");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"x={0:F1}\")=\"%s\" want=\"x=3.1\"; "
                "(\"{0:F1} units\")=\"%s\" want=\"3.1 units\" -> %s\n",
                gotPre.c_str(), gotPost.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 9 — FloatToString empty-format LARGE INTEGRAL + NaN (the printf-vs-C# divergence the refuter
  // flagged): C# v.ToString(InvariantCulture) keeps 1000000 as "1000000" (NOT %g's "1e+06"), and
  // spells NaN "NaN" (NOT printf's "nan"). injectBug drops last char → "100000" / "Na" → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotBig = cookFloatToString(pg, 1000000.0f, "");
    std::string gotNan = cookFloatToString(pg, std::nanf(""), "");
    stringInjectBug() = false;
    bool pass = (gotBig == "1000000") && (gotNan == "NaN");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1000000,\"\")=\"%s\" want=\"1000000\"; "
                "(NaN,\"\")=\"%s\" want=\"NaN\" -> %s\n",
                gotBig.c_str(), gotNan.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 10 — FloatToString custom-format on NaN + grouped "{0:N0}": custom "{0:0.000}" on NaN →
  // "NaN" (non-finite falls through to ToString); "{0:N0}" on 1234567 → "1,234,567" (',' thousands
  // grouping, C# InvariantCulture). injectBug drops last char → "Na" / "1,234,56" → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotNan = cookFloatToString(pg, std::nanf(""), "{0:0.000}");
    std::string gotGrp = cookFloatToString(pg, 1234567.0f, "{0:N0}");
    stringInjectBug() = false;
    bool pass = (gotNan == "NaN") && (gotGrp == "1,234,567");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(NaN,\"{0:0.000}\")=\"%s\" want=\"NaN\"; "
                "(1234567,\"{0:N0}\")=\"%s\" want=\"1,234,567\" -> %s\n",
                gotNan.c_str(), gotGrp.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 11 — DEFAULT (empty) format, LARGE-magnitude scientific switchover (the refuter's BLOCK-2: the
  // NON-degenerate case LEGs 1-10 全閃過 — they only tested 1e6, which sits INSIDE C#'s plain-decimal
  // band so a too-wide band still passed). C# default float ToString flips to scientific at decimal
  // exponent ≥ 7: 1e11f → "1E+11", 1e14f → "1E+14" (uppercase 'E', sign, MIN-2-digit exponent). The
  // earlier engine emitted binary-noise integers here ("99999997952") because its plain band was 1e15
  // wide and its source was (double)v, not the float shortest round-trip. injectBug drops last char →
  // "1E+1" ≠ "1E+11" → RED. (Probed vs .NET 10 / learn.microsoft.com G-specifier rules.)
  {
    stringInjectBug() = injectBug;
    std::string got11 = cookFloatToString(pg, 1e11f, "");
    std::string got14 = cookFloatToString(pg, 1e14f, "");
    stringInjectBug() = false;
    bool pass = (got11 == "1E+11") && (got14 == "1E+14");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1e11,\"\")=\"%s\" want=\"1E+11\"; "
                "(1e14,\"\")=\"%s\" want=\"1E+14\" -> %s\n",
                got11.c_str(), got14.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 12 — DEFAULT (empty) format, SMALL-magnitude scientific (the refuter's BLOCK-3): C# default
  // float ToString flips to scientific at decimal exponent < -4. 1e-5f → "1E-05"; 1.234e-5f →
  // "1.234E-05" (uppercase 'E', sign, MIN-2-digit exponent; shortest mantissa kept). The earlier
  // engine leaked %g's lowercase 2-digit form "1e-05" / "1.234e-05" — wrong CASE, faithful-looking but
  // not C#. This leg pins the case + the mantissa preservation. injectBug drops last char → "1E-0" /
  // "1.234E-0" → RED. (1.234e-5f's shortest round-trip IS "1.234E-05" — verified by the cook itself.)
  {
    stringInjectBug() = injectBug;
    std::string got5 = cookFloatToString(pg, 1e-5f, "");
    std::string gotM = cookFloatToString(pg, 1.234e-5f, "");
    stringInjectBug() = false;
    bool pass = (got5 == "1E-05") && (gotM == "1.234E-05");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(1e-5,\"\")=\"%s\" want=\"1E-05\"; "
                "(1.234e-5,\"\")=\"%s\" want=\"1.234E-05\" -> %s\n",
                got5.c_str(), gotM.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 13 — HIGH-PRECISION standard specifiers EXPOSE the float→double binary value (the refuter's
  // BLOCK-1). ★GROUND-TRUTH CORRECTION vs. this lane's work-order: the order asked for ZERO-PADDED
  // "{0:F8}" 3.14 = "3.14000000" and E10 0.1 = "1.0000000000E-001" — that is .NET FRAMEWORK 4.x
  // behaviour (old G7-then-pad). TiXL targets **net10.0** (Operators/*.csproj), and since .NET Core
  // 3.0 the F/N/E specifiers expose the EXACT IEEE-754 value: 3.14f = 3.1400001049…, 0.1f =
  // 0.10000000149…. So the faithful-to-TiXL values are the NOISE renderings, NOT the zero-padded ones:
  //   {0:F8} 3.14  → "3.14000010"          (NOT "3.14000000")
  //   {0:E10} 0.1  → "1.0000000149E-001"   (NOT "1.0000000000E-001"; standard 'E' = MIN-3-digit exp,
  //                                          distinct from default-G's 2-digit — see LEG 11/12)
  // (Confirmed against learn.microsoft.com "standard-numeric-format-strings" + the .NET Core 3.0
  //  floating-point-formatting blog.) These are NON-degenerate: they fail any engine that feeds the
  // shortest-decimal back through %f (which would zero-pad). injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string gotF8 = cookFloatToString(pg, 3.14f, "{0:F8}");
    std::string gotE10 = cookFloatToString(pg, 0.1f, "{0:E10}");
    stringInjectBug() = false;
    bool pass = (gotF8 == "3.14000010") && (gotE10 == "1.0000000149E-001");
    ok = ok && pass;
    std::printf("[selftest-stringrail] FloatToString(3.14,\"{0:F8}\")=\"%s\" want=\"3.14000010\"; "
                "(0.1,\"{0:E10}\")=\"%s\" want=\"1.0000000149E-001\" -> %s\n",
                gotF8.c_str(), gotE10.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 14 — IntToString (TiXL string/convert/IntToString.cs) DEFAULT + custom zero-pad + decimal. The
  // t3 default Format "{0:0}" = C# integer custom-numeric (min 1 digit): 42 → "42", -7 → "-7". Empty
  // format → int.ToString(InvariantCulture) = "42". "{0:000}" → zero-pad to 3 digits → "042". Hand-
  // derived against IntToString.cs. injectBug drops the last char in the REAL cook → "4" / "0" → RED.
  {
    stringInjectBug() = injectBug;
    std::string d0   = cookIntToString(pg, 42,  "{0:0}");
    std::string dNeg = cookIntToString(pg, -7,  "{0:0}");
    std::string dEmp = cookIntToString(pg, 42,  "");
    std::string dPad = cookIntToString(pg, 42,  "{0:000}");
    stringInjectBug() = false;
    bool pass = (d0 == "42") && (dNeg == "-7") && (dEmp == "42") && (dPad == "042");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 42/\"{0:0}\"=\"%s\"(42) -7=\"%s\"(-7) 42/\"\"=\"%s\"(42) "
                "42/\"{0:000}\"=\"%s\"(042) -> %s\n",
                d0.c_str(), dNeg.c_str(), dEmp.c_str(), dPad.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 15 — IntToString STANDARD specifiers D/N/X (the integer-oriented vocabulary, vs FloatToString's
  // F/N/E). "{0:N0}" 1234567 → "1,234,567" (',' grouping); "{0:X}" 255 → "FF"; "{0:X}" -1 → "FFFFFFFF"
  // (two's-complement 32-bit hex of a negative int, ported verbatim). Hand-derived against C#
  // InvariantCulture. injectBug drops the last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string grp  = cookIntToString(pg, 1234567, "{0:N0}");
    std::string hex  = cookIntToString(pg, 255,     "{0:X}");
    std::string hexN = cookIntToString(pg, -1,      "{0:X}");
    stringInjectBug() = false;
    bool pass = (grp == "1,234,567") && (hex == "FF") && (hexN == "FFFFFFFF");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 1234567/\"{0:N0}\"=\"%s\"(1,234,567) 255/\"{0:X}\"=\"%s\"(FF) "
                "-1/\"{0:X}\"=\"%s\"(FFFFFFFF) -> %s\n",
                grp.c_str(), hex.c_str(), hexN.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 16 — IntToString COMPOSITE LITERAL (surrounding text kept verbatim, placeholder substituted) +
  // INVALID FORMAT fallback (TiXL's FormatException catch). "id={0:000}!" on 42 → "id=042!";
  // "{0:Q}" (unrecognised spec) → "Invalid Format" (NOT a silent passthrough). injectBug drops last char.
  {
    stringInjectBug() = injectBug;
    std::string lit = cookIntToString(pg, 42, "id={0:000}!");
    std::string inv = cookIntToString(pg, 42, "{0:Q}");
    stringInjectBug() = false;
    // Note: injectBug also drops the last char of "Invalid Format" → "Invalid Forma" ≠ → RED (good: the
    // fallback string is a REAL cook output, so the tooth bites it too).
    bool pass = (lit == "id=042!") && (inv == "Invalid Format");
    ok = ok && pass;
    std::printf("[selftest-stringrail] IntToString 42/\"id={0:000}!\"=\"%s\"(id=042!) 42/\"{0:Q}\"=\"%s\""
                "(Invalid Format) -> %s\n",
                lit.c_str(), inv.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 17 — Vec3ToString (TiXL string/convert/Vec3ToString.cs) DEFAULT (empty) format = the fixed form
  // "X: {0,7:F2}\nY: {1,7:F2}\nZ: {2,7:F2}" — each component F2, right-aligned to 7, real newlines.
  // (1.5,-2,3.25) → "X:    1.50\nY:   -2.00\nZ:    3.25". Hand-derived against the .cs default branch.
  // injectBug drops the last char ("3.25"→"3.2…") → RED.
  {
    stringInjectBug() = injectBug;
    std::string got = cookVec3ToString(pg, 1.5f, -2.0f, 3.25f, "");
    stringInjectBug() = false;
    bool pass = (got == "X:    1.50\nY:   -2.00\nZ:    3.25");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString(1.5,-2,3.25,\"\")=[%s] want=[X:    1.50/Y:   -2.00/Z:    "
                "3.25] -> %s\n",
                got.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 18 — Vec3ToString CUSTOM format: placeholder reorder/substitution + the literal "\n"→newline
  // replacement (Vec3ToString.cs s.Replace("\\n","\n")). "({0},{1},{2})" on (1,2,3) → "(1,2,3)" (bare
  // {N} → C# float ToString, integer-valued → "1"/"2"/"3"). "{0:F1}|{1:F1}|{2:F1}" on (1.5,2.5,3.5) →
  // "1.5|2.5|3.5". injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string bare = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "({0},{1},{2})");
    std::string f1   = cookVec3ToString(pg, 1.5f, 2.5f, 3.5f, "{0:F1}|{1:F1}|{2:F1}");
    stringInjectBug() = false;
    bool pass = (bare == "(1,2,3)") && (f1 == "1.5|2.5|3.5");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString custom (1,2,3)=\"%s\"((1,2,3)) F1=\"%s\"(1.5|2.5|3.5) "
                "-> %s\n",
                bare.c_str(), f1.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 19 — Vec3ToString INVALID FORMAT fallback (TiXL's FormatException catch): an out-of-vocabulary
  // format (index ≥3 / unsupported spec) → "Invalid Format". "{3}" (no 4th arg) → invalid; "{0:Z}"
  // (unknown spec) → invalid. injectBug drops last char → RED.
  {
    stringInjectBug() = injectBug;
    std::string i3 = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "{3}");
    std::string iz = cookVec3ToString(pg, 1.0f, 2.0f, 3.0f, "{0:Z}");
    stringInjectBug() = false;
    bool pass = (i3 == "Invalid Format") && (iz == "Invalid Format");
    ok = ok && pass;
    std::printf("[selftest-stringrail] Vec3ToString \"{3}\"=\"%s\" \"{0:Z}\"=\"%s\" want=Invalid Format -> %s\n",
                i3.c_str(), iz.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 20 — ★R-2 PRODUCTION RESIDENT path (the whole point of task_32b5b6e5): the SAME CombineStrings
  // graph (FloatToString producers → CombineStrings.Input, a GENUINE resident String wire) driven
  // through libFromGraph → buildEvalGraph → cookStringNodes (the per-frame pass the running app drives)
  // → extStrOut. Mirrors the FLAT legs 3/4: [1,2,3] joins "1-2-3" AND wire-order [3,1,2] joins "3-1-2"
  // (NOT sorted, NOT first-wire-only). Proves the String currency is LIVE on the production resident
  // path — before the resident string-wire rail, the flatten DROPPED the FloatToString.Output →
  // CombineStrings.Input wire, so the resident gather read the unwired-const fallback (empty) and
  // joined "" — the flat-only self-deception this gate kills. injectBug corrupts the REAL cook on the
  // resident path too: FloatToString (the producer) drops its last char in cook AND CombineStrings
  // drops its last char → the joined result is wrong → RED on the SAME StringCookFn that runs flat.
  {
    stringInjectBug() = injectBug;
    std::string gotSeq = cookCombineResident({1.0f, 2.0f, 3.0f}, "-");
    std::string gotWire = cookCombineResident({3.0f, 1.0f, 2.0f}, "-");
    stringInjectBug() = false;
    bool pass = (gotSeq == "1-2-3") && (gotWire == "3-1-2");
    ok = ok && pass;
    std::printf("[selftest-stringrail] RESIDENT(production) CombineStrings([1,2,3],\"-\")=\"%s\" want=\"1-2-3\"; "
                "([3,1,2],\"-\")=\"%s\" want=\"3-1-2\" (wire-decl) -> %s\n",
                gotSeq.c_str(), gotWire.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 21 — ★RESIDENT StringLength (refuter followup — closes the gate): StringLength is the named
  // production consumer of the resident string-wire seam (task_32b5b6e5). Before the rail, the flatten
  // DROPPED its String wire so cookHostScalarNodes skipped it (extOut[0] stayed 0). Now the flatten
  // projects a Connection driver onto the String slot and cookHostScalarNodes reads the WIRED upstream
  // via cookResidentString → .size() → extOut[0]. Two sub-cases (mirror of flat LEGs 5/6):
  //   WIRED: FloatToString(3.14,"") → "3.14" (4 chars) → StringLength.InputString → extOut[0] = 4.0
  //   CONST: StringLength.InputString strDef = "Line\nLine" (9 chars) → extOut[0] = 9.0
  // Call order (the production sequence): cookStringNodes first (drives FloatToString → extStrOut),
  // THEN cookHostScalarNodes (StringLength gathers via cookResidentString → .size() → extOut[0]).
  // injectBug corrupts the upstream cook (FloatToString drops its last char → shorter string) AND
  // then the StringLength branch clears extOut[0] to 0.0 — both sub-cases go → 0.0 under bug → RED.
  {
    // WIRED sub-case: FloatToString(3.14,"") wired into StringLength.InputString.
    // Graph: StringLength id=1 (terminal), FloatToString id=2 (producer).
    // StringLength ports: [0]=Length(out), [1]=InputString. FloatToString ports: [0]=Output(out), others.
    auto makeStringLengthGraph = [](bool wired, const std::string& constText) {
      Graph g;
      Node sl; sl.id = 1; sl.type = "StringLength";
      if (!wired) sl.strParams["InputString"] = constText;  // UNWIRED → strDef const (via libFromGraph)
      g.nodes.push_back(sl);
      if (wired) {
        Node fts; fts.id = 2; fts.type = "FloatToString";
        fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14" (4 chars)
        g.nodes.push_back(fts);
        // FloatToString.Output (port 0) → StringLength.InputString (port 1).
        g.connections.push_back({400, pinId(2, /*out*/ 0), pinId(1, /*InputString*/ 1)});
        g.nextId = 3;
      } else {
        g.nextId = 2;
      }
      return g;
    };

    stringInjectBug() = injectBug;
    // Wired sub-case.
    float gotWired = [&]() -> float {
      Graph g = makeStringLengthGraph(/*wired=*/true, "");
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);      // 1st: cooks FloatToString → extStrOut (the String wire source)
      cookHostScalarNodes(rg, rc);  // 2nd: StringLength gathers via cookResidentString → extOut[0]
      const ResidentNode* n = rg.node("1");  // StringLength resident path == flat node id "1"
      return n ? n->extOut[0] : -1.0f;
    }();
    // Const sub-case (unwired → strDef fallback "Line\nLine", 9 chars, matching flat LEG 6).
    float gotConst = [&]() -> float {
      Graph g = makeStringLengthGraph(/*wired=*/false, "Line\nLine");
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);      // no-op for const-only graph (no String producers to cook)
      cookHostScalarNodes(rg, rc);  // StringLength reads strInputs[InputString] → "Line\nLine" → 9
      const ResidentNode* n = rg.node("1");
      return n ? n->extOut[0] : -1.0f;
    }();
    stringInjectBug() = false;

    bool passWired = std::fabs(gotWired - 4.0f) < 1e-5f;  // wired "3.14" → 4 chars; bug → 0.0
    bool passConst = std::fabs(gotConst - 9.0f) < 1e-5f;  // const "Line\nLine" → 9 chars; bug → 0.0
    bool pass = passWired && passConst;
    ok = ok && pass;
    std::printf("[selftest-stringrail] RESIDENT(production) StringLength(wired \"3.14\")=%.1f want=4.0; "
                "(const \"Line\\nLine\")=%.1f want=9.0 -> %s\n",
                gotWired, gotConst, pass ? "PASS" : "FAIL");
  }

  // LEGs 22-24 reserved for future / sibling-lane String-op expansion. No body needed.

  // LEG 25 — IndexOf: TWO-String-input host-scalar op (String×String → Int dissolved to Float). Tests
  // BOTH the FLAT path (point_graph.cpp cookHostScalar branch, which gathers both String inputs via
  // gatherStringInputs) AND the RESIDENT path (cookHostScalarNodes IndexOf dedicated branch, which
  // gathers via cookResidentString on the resident String-wire rail). TiXL semantics (IndexOf.cs):
  //   • either input IsNullOrEmpty → -1
  //   • else originalString.IndexOf(searchPattern) = first occurrence, -1 if not found
  //
  // Hand-derived expected values:
  //   "hello world".IndexOf("world") = 6  (substring starts at index 6: h=0,e=1,l=2,l=3,o=4,' '=5,w=6)
  //   "hello world".IndexOf("xyz")   = -1 (not found)
  //   "hello world".IndexOf("")      = -1 (searchPattern IsNullOrEmpty → -1 per TiXL .cs:26)
  //
  // For the RESIDENT leg, we wire at least one String input through a FloatToString producer — the
  // OriginalString port — proving the resident String wire is live. SearchPattern is wired as a const
  // (strParams["SearchPattern"] = "world") to keep the graph minimal but still prove gather of both.
  // A fully-wired-both variant would duplicate LEG 20's coverage; const-for-second is sufficient.
  //
  // injectBug path: hostScalarInjectBug() inside cookIndexOf (flat) and the resident IndexOf branch
  // each write -999.0f (a sentinel diverging from any valid TiXL result) → the golden sees a value
  // ≠ 6.0f (or ≠ -1.0f) → FAIL. Under bug, stringInjectBug() also fires on the upstream
  // FloatToString producer, shortening the original string — the resident leg then computes
  // IndexOf on a corrupted original (short string won't contain "world") → -1.0f ≠ 6.0f → RED
  // even without the sentinel, but the sentinel from hostScalarInjectBug() fires first.
  //
  // NOTE: hostScalarInjectBug() must be set; stringInjectBug() is set here too so the upstream
  // FloatToString cook (resident cookStringNodes pass) is also corrupted — belt-and-suspenders,
  // mirrors LEG 21 which sets stringInjectBug() for the FloatToString upstream.
  {
    // --- FLAT sub-cases ---
    {
      // Case 1: found
      hostScalarInjectBug() = injectBug;
      stringInjectBug() = injectBug;
      float gotFound = [&]() -> float {
        Graph g;
        Node ix; ix.id = 1; ix.type = "IndexOf";
        ix.strParams["OriginalString"] = "hello world";
        ix.strParams["SearchPattern"]  = "world";
        g.nodes.push_back(ix);
        EvaluationContext ctx{};
        ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
        pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
        const std::vector<float>* out = pg.debugCookedFloatList(1);
        return (out && !out->empty()) ? (*out)[0] : -999.0f;
      }();
      hostScalarInjectBug() = false;
      stringInjectBug() = false;
      bool passFound = std::fabs(gotFound - 6.0f) < 1e-5f;
      ok = ok && passFound;
      std::printf("[selftest-stringrail] IndexOf FLAT(\"hello world\",\"world\")=%.1f want=6.0 -> %s\n",
                  gotFound, passFound ? "PASS" : "FAIL");
    }
    {
      // Case 2: not found
      hostScalarInjectBug() = injectBug;
      stringInjectBug() = injectBug;
      float gotMiss = [&]() -> float {
        Graph g;
        Node ix; ix.id = 1; ix.type = "IndexOf";
        ix.strParams["OriginalString"] = "hello world";
        ix.strParams["SearchPattern"]  = "xyz";
        g.nodes.push_back(ix);
        EvaluationContext ctx{};
        ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
        pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
        const std::vector<float>* out = pg.debugCookedFloatList(1);
        return (out && !out->empty()) ? (*out)[0] : -999.0f;
      }();
      hostScalarInjectBug() = false;
      stringInjectBug() = false;
      bool passMiss = std::fabs(gotMiss - (-1.0f)) < 1e-5f;
      ok = ok && passMiss;
      std::printf("[selftest-stringrail] IndexOf FLAT(\"hello world\",\"xyz\")=%.1f want=-1.0 -> %s\n",
                  gotMiss, passMiss ? "PASS" : "FAIL");
    }

    // --- RESIDENT sub-case (R-2 production path) ---
    // OriginalString WIRED from FloatToString(3.14f,"") → "3.14"; SearchPattern const "14".
    //   IndexOf("3.14", "14") = 2  (h.v.: "3.14"[0]='3',[1]='.',[2]='1',[3]='4'; "14" starts at 2)
    hostScalarInjectBug() = injectBug;
    stringInjectBug() = injectBug;
    float gotResident = [&]() -> float {
      // Graph:
      //   id=1: IndexOf — OriginalString wired from id=2; SearchPattern = strParams["SearchPattern"]="14"
      //   id=2: FloatToString(3.14f, "") → "3.14" (the WIRED upstream String producer)
      // IndexOf ports: [0]=Index(out), [1]=OriginalString, [2]=SearchPattern.
      // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format.
      Graph g;
      Node ix; ix.id = 1; ix.type = "IndexOf";
      ix.strParams["SearchPattern"] = "14";  // const path for SearchPattern (tests wire-OR-const dual)
      g.nodes.push_back(ix);

      Node fts; fts.id = 2; fts.type = "FloatToString";
      fts.params["Value"] = 3.14f; fts.strParams["Format"] = "";  // → "3.14"
      g.nodes.push_back(fts);

      // Wire FloatToString.Output (port 0) → IndexOf.OriginalString (port 1).
      g.connections.push_back({500, pinId(2, /*out*/ 0), pinId(1, /*OriginalString*/ 1)});
      g.nextId = 3;

      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);      // 1st: cooks FloatToString → extStrOut ("3.14" on String wire)
      cookHostScalarNodes(rg, rc);  // 2nd: IndexOf gathers via cookResidentString → find("14") → extOut[0]
      const ResidentNode* n = rg.node("1");  // IndexOf resident path == flat node id "1"
      return n ? n->extOut[0] : -999.0f;
    }();
    hostScalarInjectBug() = false;
    stringInjectBug() = false;

    bool passResident = std::fabs(gotResident - 2.0f) < 1e-5f;
    ok = ok && passResident;
    std::printf("[selftest-stringrail] IndexOf RESIDENT(wired \"3.14\", const \"14\")=%.1f want=2.0 -> %s\n",
                gotResident, passResident ? "PASS" : "FAIL");
  }

  // LEG 26 — SubString FLAT + RESIDENT (TiXL string/search/SubString.cs).
  //
  // FLAT sub-cases (hand-derived against SubString.cs):
  //   (A) Normal: SubString("hello world", start=6, length=5):
  //       clampStart = Clamp(6, 0, 11) = 6; clampedLen = Clamp(5, 0, 5) = 5
  //       NOT early-exit; start≠0 → Substring(6,5) = "world"  ← expected "world"
  //   (B) Negative-start clamp: SubString("hello world", start=-3, length=5):
  //       clampStart = Clamp(-3, 0, 11) = 0; clampedLen = Clamp(5, 0, 11) = 5
  //       NOT early-exit; start≠0 (unclamped -3) → Substring(0,5) = "hello"  ← expected "hello"
  //       NOTE: fast-path requires UNCLAMPED start==0, which is not true here (-3≠0) → normal path.
  //   (C) Length-overrun clamp: SubString("hello world", start=7, length=100):
  //       clampStart = Clamp(7, 0, 11) = 7; clampedLen = Clamp(100, 0, 4) = 4
  //       NOT early-exit; start≠0 → Substring(7,4) = "orld"   ← expected "orld"
  //
  // RESIDENT sub-case (proves resident String wire feeds InputText):
  //   FloatToString(12345.0f, "") wired into SubString.InputText, Start=1, Length=3:
  //     FloatToString(12345, "") → "12345" (5 chars; 12345.0f integer-valued, plain decimal band E=4)
  //     SubString("12345", start=1, length=3):
  //       clampStart = Clamp(1, 0, 5) = 1; clampedLen = Clamp(3, 0, 4) = 3
  //       NOT early-exit; start≠0 → Substring(1,3) = "234"    ← expected "234"
  //
  // RED (injectBug): stringInjectBug() corrupts the REAL cook: SubString drops its last char; on the
  // resident path FloatToString also drops its last char BEFORE SubString even runs (the upstream
  // producer is corrupted), so the substring is taken from the already-shortened string — both sub-
  // cases go RED. No `want` inversion — the bug bites the actual cook path.
  //
  // FLAT legs:
  {
    auto cookSubStr = [&](const std::string& text, int start, int length) -> std::string {
      Graph g;
      Node n; n.id = 1; n.type = "SubString";
      n.strParams["InputText"] = text;   // unwired → strDef const (InputText carries the string)
      n.params["Start"]  = (float)start;
      n.params["Length"] = (float)length;
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };

    stringInjectBug() = injectBug;
    std::string gA = cookSubStr("hello world",  6,   5);   // (A) "world"
    std::string gB = cookSubStr("hello world", -3,   5);   // (B) "hello"  (negative-start clamp)
    std::string gC = cookSubStr("hello world",  7, 100);   // (C) "orld"   (length-overrun clamp)
    stringInjectBug() = false;

    bool passA = (gA == "world");
    bool passB = (gB == "hello");
    bool passC = (gC == "orld");
    bool pass  = passA && passB && passC;
    ok = ok && pass;
    std::printf("[selftest-stringrail] SubString FLAT (A)=\"%s\" want=\"world\"; "
                "(B)=\"%s\" want=\"hello\"; (C)=\"%s\" want=\"orld\" -> %s\n",
                gA.c_str(), gB.c_str(), gC.c_str(), pass ? "PASS" : "FAIL");
  }

  // RESIDENT leg: FloatToString(12345,"") wired into SubString.InputText, Start=1, Length=3 → "234".
  {
    // Graph: SubString id=1 (terminal), FloatToString id=2 (String producer).
    // SubString ports: [0]=Result(out), [1]=InputText(String in), [2]=Start(Float), [3]=Length(Float).
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format.
    // Wire: FloatToString.Output (port 0) → SubString.InputText (port 1).
    Graph g;
    Node sub; sub.id = 1; sub.type = "SubString";
    sub.params["Start"]  = 1.0f;
    sub.params["Length"] = 3.0f;
    // InputText is WIRED (no strParams["InputText"] — the wire drives it on the resident path).
    g.nodes.push_back(sub);

    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 12345.0f; fts.strParams["Format"] = "";  // → "12345" (5 chars)
    g.nodes.push_back(fts);
    // FloatToString.Output (port 0) → SubString.InputText (port 1).
    g.connections.push_back({500, pinId(2, /*out*/ 0), pinId(1, /*InputText*/ 1)});
    g.nextId = 3;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut["2"][0], THEN SubString reads it
      const ResidentNode* n = rg.node("1");  // SubString resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    bool pass = (gotResident == "234");
    ok = ok && pass;
    std::printf("[selftest-stringrail] SubString RESIDENT FloatToString(12345)→InputText[1:3]=\"%s\" "
                "want=\"234\" -> %s\n",
                gotResident.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 27 — ChangeCase (TiXL string/transform/ChangeCase.cs): InputText + Mode(enum) → String.
  // Modes: 0=ToUpperCase (str?.ToUpperInvariant()), 1=ToLowerCase (str?.ToLowerInvariant()).
  // Hand-derived against ChangeCase.cs + fork-changecase-invariant-culture (ASCII range, byte-identical
  // to C# InvariantCulture — "Hello World" ToUpper → "HELLO WORLD", ToLower → "hello world").
  //
  // FLAT sub-leg A: ChangeCase("Hello World", ToUpper=0) → "HELLO WORLD" (const strDef path).
  // FLAT sub-leg B: ChangeCase("Hello World", ToLower=1) → "hello world" (const strDef path).
  // RESIDENT sub-leg C: ChangeCase(strDef="Hello World", ToUpper=0) via resident path → "HELLO WORLD"
  //   (proves the resident flatten supplies strDef const to ChangeCase's InputText String port).
  // RESIDENT sub-leg D: FloatToString(42,"") → "42" wired into ChangeCase.InputText, ToLower=1 →
  //   "42" (digits unchanged, but the WIRE is genuine resident → proves InputText String wire live).
  // injectBug drops the last char in the REAL cook output for all sub-legs → any expected non-empty
  // string loses its last char → mismatch → RED on the actual cook path (not inverted want).
  {
    stringInjectBug() = injectBug;
    // Flat A: ToUpper
    std::string flatUp = cookChangeCaseFlat(pg, "Hello World", /*ToUpperCase=*/0);
    // Flat B: ToLower
    std::string flatLo = cookChangeCaseFlat(pg, "Hello World", /*ToLowerCase=*/1);
    // Resident C: const-path ToUpper
    std::string resUp  = cookChangeCaseResident(/*wired=*/false, "Hello World", /*ToUpperCase=*/0);
    // Resident D: wired FloatToString → ToLower (digits → "42")
    std::string resWire = cookChangeCaseResident(/*wired=*/true, "", /*ToLowerCase=*/1);
    stringInjectBug() = false;
    bool pass = (flatUp == "HELLO WORLD") && (flatLo == "hello world") &&
                (resUp  == "HELLO WORLD") && (resWire == "42");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG27 ChangeCase flat ToUpper=\"%s\" want=\"HELLO WORLD\"; "
                "flat ToLower=\"%s\" want=\"hello world\"; "
                "res ToUpper=\"%s\" want=\"HELLO WORLD\"; "
                "res wired ToLower=\"%s\" want=\"42\" -> %s\n",
                flatUp.c_str(), flatLo.c_str(), resUp.c_str(), resWire.c_str(),
                pass ? "PASS" : "FAIL");
  }

  // LEG 28 — StringRepeat FLAT + RESIDENT (TiXL string/combine/StringRepeat.cs).
  //
  // Semantics (StringRepeat.cs): count = Clamp(rawCount, 0, 1000); if count==0 OR content empty → "";
  // else content repeated `count` times (StringBuilder().Insert(0, content, count)).
  //
  // FLAT sub-cases (hand-derived):
  //   (A) Repeat: StringRepeat("ab", 3) → "ababab"          (3 concatenations)
  //   (B) Zero count: StringRepeat("x", 0) → ""             (count==0 early-empty)
  //   (C) Negative count clamp: StringRepeat("ab", -2) → "" (Clamp(-2,0,1000)=0 → count==0 → "")
  //
  // RESIDENT sub-case (proves resident String wire feeds Fragment):
  //   FloatToString(7.0f, "") → "7" wired into StringRepeat.Fragment, Count=3 → "777".
  //
  // RED (injectBug): stringInjectBug() corrupts the REAL cook (StringRepeat drops its last char); on
  // the resident path FloatToString also drops its last char BEFORE StringRepeat runs. No want-inversion.
  //
  // FLAT legs:
  {
    auto cookRepeat = [&](const std::string& fragment, int count) -> std::string {
      Graph g;
      Node n; n.id = 1; n.type = "StringRepeat";
      n.strParams["Fragment"] = fragment;   // unwired → strDef const (Fragment carries the string)
      n.params["Count"] = (float)count;
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };

    stringInjectBug() = injectBug;
    std::string gA = cookRepeat("ab",  3);   // (A) "ababab"
    std::string gB = cookRepeat("x",   0);   // (B) ""
    std::string gC = cookRepeat("ab", -2);   // (C) "" (negative clamp → 0)
    stringInjectBug() = false;

    bool passA = (gA == "ababab");
    bool passB = (gB == "");
    bool passC = (gC == "");
    bool pass  = passA && passB && passC;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG28 StringRepeat FLAT (A)=\"%s\" want=\"ababab\"; "
                "(B)=\"%s\" want=\"\"; (C)=\"%s\" want=\"\" -> %s\n",
                gA.c_str(), gB.c_str(), gC.c_str(), pass ? "PASS" : "FAIL");
  }
  // RESIDENT leg: FloatToString(7,"") wired into StringRepeat.Fragment, Count=3 → "777".
  {
    // Graph: StringRepeat id=1 (terminal), FloatToString id=2 (String producer).
    // StringRepeat ports: [0]=Result(out), [1]=Fragment(String in), [2]=Count(Float).
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format.
    Graph g;
    Node rep; rep.id = 1; rep.type = "StringRepeat";
    rep.params["Count"] = 3.0f;
    // Fragment is WIRED (no strParams["Fragment"] — the wire drives it on the resident path).
    g.nodes.push_back(rep);

    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 7.0f; fts.strParams["Format"] = "";  // → "7"
    g.nodes.push_back(fts);
    // FloatToString.Output (port 0) → StringRepeat.Fragment (port 1).
    g.connections.push_back({600, pinId(2, /*out*/ 0), pinId(1, /*Fragment*/ 1)});
    g.nextId = 3;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut["2"][0], THEN StringRepeat reads it
      const ResidentNode* n = rg.node("1");  // StringRepeat resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    bool pass = (gotResident == "777");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG28 StringRepeat RESIDENT FloatToString(7)→Fragment×3=\"%s\" "
                "want=\"777\" -> %s\n",
                gotResident.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 29 — StringInsert FLAT + RESIDENT (TiXL string/buffers/transform/StringInsert.cs).
  //
  // Semantics: maxPos = original.Length - insert.Length; if original/insert empty OR maxPos<=0 → unset
  // (→ "" here, fork-…-unset-becomes-empty). position (Int); if UseModuloPosition → Abs(pos)%maxPos;
  // else position.Clamp(0,maxPos) is a DISCARDED no-op (fork-…-clamp-noop) → raw position used; then
  // Remove(position, insert.Length).Insert(position, insert) [= overwrite insert.Length chars at pos],
  // out-of-range → C# throws → caught → unset (→ "").
  //
  // FLAT sub-cases (hand-derived, verified against the .cs Remove-then-Insert splice):
  //   (A) Pos 0: StringInsert("hello world","XYZ",0,modulo=false): maxPos=8; Remove(0,3)="lo world",
  //       Insert(0,"XYZ") → "XYZlo world"
  //   (B) Mid: StringInsert("hello world","XY",6,modulo=false): maxPos=9; Remove(6,2)="hello rld",
  //       Insert(6,"XY") → "hello XYrld"
  //   (C) Modulo wrap: StringInsert("abcdef","XY",10,modulo=true): maxPos=4; pos=Abs(10)%4=2;
  //       Remove(2,2)="abef", Insert(2,"XY") → "abXYef"
  //   (D) ★FAITHFUL-BUG out-of-range non-modulo: StringInsert("abcdef","XY",10,modulo=false): maxPos=4;
  //       NO clamp (the discarded-return bug) → pos=10; 10+2>6 → Remove would throw → caught → ""
  //
  // RESIDENT sub-case (proves resident String wire feeds Original; Insertion rides the const path):
  //   FloatToString(12345.0f,"") → "12345" wired into Original; Insertion const "AB", Position=1,
  //   modulo=false: maxPos=3; Remove(1,2)="145", Insert(1,"AB") → "1AB45".
  //
  // RED (injectBug): stringInjectBug() corrupts the REAL cook (StringInsert drops last char); on the
  // resident path the upstream FloatToString drops its last char first. The (D) out-of-range case
  // already yields "" so the bug-drop is a no-op there — but (A)/(B)/(C)/RESIDENT all bite. No inversion.
  //
  // FLAT legs:
  {
    auto cookInsert = [&](const std::string& original, const std::string& insertion, int position,
                          bool modulo) -> std::string {
      Graph g;
      Node n; n.id = 1; n.type = "StringInsert";
      n.strParams["Original"]  = original;    // unwired → strDef const
      n.strParams["Insertion"] = insertion;   // unwired → strDef const
      n.params["Position"]          = (float)position;
      n.params["UseModuloPosition"] = modulo ? 1.0f : 0.0f;  // bool dissolved to Float
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };

    stringInjectBug() = injectBug;
    std::string gA = cookInsert("hello world", "XYZ", 0,  false);  // (A) "XYZlo world"
    std::string gB = cookInsert("hello world", "XY",  6,  false);  // (B) "hello XYrld"
    std::string gC = cookInsert("abcdef",      "XY",  10, true);   // (C) "abXYef" (modulo)
    std::string gD = cookInsert("abcdef",      "XY",  10, false);  // (D) "" (faithful OOR bug)
    stringInjectBug() = false;

    bool passA = (gA == "XYZlo world");
    bool passB = (gB == "hello XYrld");
    bool passC = (gC == "abXYef");
    bool passD = (gD == "");
    bool pass  = passA && passB && passC && passD;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG29 StringInsert FLAT (A)=\"%s\" want=\"XYZlo world\"; "
                "(B)=\"%s\" want=\"hello XYrld\"; (C)=\"%s\" want=\"abXYef\"; "
                "(D OOR-bug)=\"%s\" want=\"\" -> %s\n",
                gA.c_str(), gB.c_str(), gC.c_str(), gD.c_str(), pass ? "PASS" : "FAIL");
  }
  // RESIDENT leg: FloatToString(12345,"") wired into Original; Insertion const "AB", Pos=1 → "1AB45".
  {
    // Graph: StringInsert id=1 (terminal), FloatToString id=2 (String producer for Original).
    // StringInsert ports: [0]=Result(out), [1]=Original(String in), [2]=Insertion(String in),
    //                     [3]=Position(Float), [4]=UseModuloPosition(Float).
    // FloatToString ports: [0]=Output(out), [1]=Value, [2]=Format.
    Graph g;
    Node ins; ins.id = 1; ins.type = "StringInsert";
    ins.strParams["Insertion"]        = "AB";  // const path for Insertion (wire-OR-const dual)
    ins.params["Position"]            = 1.0f;
    ins.params["UseModuloPosition"]   = 0.0f;
    // Original is WIRED (no strParams["Original"] — the wire drives it on the resident path).
    g.nodes.push_back(ins);

    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 12345.0f; fts.strParams["Format"] = "";  // → "12345" (5 chars)
    g.nodes.push_back(fts);
    // FloatToString.Output (port 0) → StringInsert.Original (port 1).
    g.connections.push_back({700, pinId(2, /*out*/ 0), pinId(1, /*Original*/ 1)});
    g.nextId = 3;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut["2"][0], THEN StringInsert reads it
      const ResidentNode* n = rg.node("1");  // StringInsert resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    bool pass = (gotResident == "1AB45");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG29 StringInsert RESIDENT FloatToString(12345)→Original, "
                "Insertion=\"AB\" @1 =\"%s\" want=\"1AB45\" -> %s\n",
                gotResident.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 30 — MockStrings FLAT + RESIDENT (TiXL string/random/MockStrings.cs).
  //
  // DETERMINISTIC (NOT stateful): Result = _strings[MathUtils.Mod(Category, 15)]. Pure function of
  // Category (enum index into a fixed 15-entry array, ported verbatim). MathUtils.Mod is euclidean
  // (negative category wraps to a non-negative index).
  //
  // FLAT sub-cases — assert STRUCTURAL anchors of the verbatim arrays (the first token before the
  // first '\n', which is byte-exact ground-truth from the .cs):
  //   (A) Category=0 (Colors)        → array[0]  begins "Black\n…"   → first line == "Black"
  //   (B) Category=2 (Females)       → array[2]  begins "Mary\n…"    → first line == "Mary"
  //   (C) Category=9 (ValuesToRates) → array[9]  == "0\n0.125\n…\n32" → first line == "0", last == "32"
  //   (D) ★euclidean-mod wrap: Category=15 → Mod(15,15)=0 → array[0] (== case A, "Black\n…")
  //   (E) ★euclidean-mod negative: Category=-1 → Mod(-1,15)=14 (BalancedPrimes) → array[14] first=="5"
  //
  // RESIDENT sub-case: MockStrings has NO String input — Category rides the resolved Float param on the
  // resident path. Category=3 (Males) → array[3] begins "James\n…" → first line == "James". This proves
  // the resident Float-param spine reaches a zero-String-input producer's cook (extStrOut written).
  //
  // RED (injectBug): stringInjectBug() drops the last char of the REAL cooked string (e.g. the trailing
  // "32" of ValuesToRates loses its '2'; "Black\n…White" loses its 'e'). We assert the FULL strings for
  // the small arrays (C → exact) and first-line anchors for the big ones; the bug-drop changes the tail
  // → first-line anchors still match for some, so case (C) asserts the EXACT full string (its tail
  // "…\n32" loses '2' under bug → "…\n3" ≠ → RED), guaranteeing the tooth bites. No want-inversion.
  {
    auto cookMock = [&](int category) -> std::string {
      Graph g;
      Node n; n.id = 1; n.type = "MockStrings";
      n.params["Category"] = (float)category;  // enum index stored as Float (int-on-Float contract)
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };
    auto firstLine = [](const std::string& s) -> std::string {
      auto nl = s.find('\n');
      return nl == std::string::npos ? s : s.substr(0, nl);
    };

    stringInjectBug() = injectBug;
    std::string gColors = cookMock(0);    // (A) Colors  → "Black\n…"
    std::string gFemale = cookMock(2);    // (B) Females → "Mary\n…"
    std::string gRates  = cookMock(9);    // (C) ValuesToRates → exact "0\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32"
    std::string gWrap   = cookMock(15);   // (D) Mod(15,15)=0 → Colors
    std::string gNeg    = cookMock(-1);   // (E) Mod(-1,15)=14 → BalancedPrimes → "5\n…"
    stringInjectBug() = false;

    bool passA = (firstLine(gColors) == "Black");
    bool passB = (firstLine(gFemale) == "Mary");
    // (C) EXACT full-string assertion (the verbatim small array — also the bug-bite anchor):
    bool passC = (gRates == "0\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32");
    bool passD = (firstLine(gWrap) == "Black");                     // euclidean wrap to index 0
    bool passE = (firstLine(gNeg)  == "5");                         // euclidean negative wrap to index 14
    bool pass  = passA && passB && passC && passD && passE;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG30 MockStrings FLAT (A Colors)first=\"%s\" want=\"Black\"; "
                "(B Females)first=\"%s\" want=\"Mary\"; (C ValuesToRates)exact=%s; "
                "(D Mod15→0)first=\"%s\" want=\"Black\"; (E Mod-1→14)first=\"%s\" want=\"5\" -> %s\n",
                firstLine(gColors).c_str(), firstLine(gFemale).c_str(), passC ? "OK" : "MISMATCH",
                firstLine(gWrap).c_str(), firstLine(gNeg).c_str(), pass ? "PASS" : "FAIL");
  }
  // RESIDENT leg: MockStrings Category=9 (ValuesToRates) via the resident Float-param spine. We assert
  // the EXACT verbatim array (so the injectBug last-char drop bites the resident leg too — its tail
  // "…\n32" loses '2' under bug). This proves the resident Float-param spine reaches a zero-String-input
  // producer's cook (extStrOut written), AND the resident path is bug-bitten alongside the flat path.
  {
    // Graph: MockStrings id=1 (terminal), ZERO String inputs. Category rides the resolved Float param.
    Graph g;
    Node mk; mk.id = 1; mk.type = "MockStrings";
    mk.params["Category"] = 9.0f;  // ValuesToRates (exact small array)
    g.nodes.push_back(mk);
    g.nextId = 2;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // MockStrings cooks (no String input; reads resolved Category param)
      const ResidentNode* n = rg.node("1");  // MockStrings resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    auto nl = gotResident.find('\n');
    std::string first = nl == std::string::npos ? gotResident : gotResident.substr(0, nl);
    bool pass = (gotResident == "0\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG30 MockStrings RESIDENT Category=9(ValuesToRates) first=\"%s\" "
                "exact=%s -> %s\n",
                first.c_str(), pass ? "OK" : "MISMATCH", pass ? "PASS" : "FAIL");
  }

  // LEG 31 — PickString FLAT + RESIDENT (TiXL string/logic/PickString.cs). MultiInput<string> + Index
  // → selects inputStrings[Index.Mod(count)] (euclidean modulo, NOT C++ truncating %). This is the
  // SAME MultiInput gather as CombineStrings (LEG 20) but SELECTS one wire instead of joining all.
  //
  // FLAT sub-cases (producers are FloatToString of integer values → "10"/"20"/"30"/...; wire-decl
  // order is the gather order — the load-bearing contract proven for CombineStrings):
  //   (A) Pick mid: producers [10,20,30], Index=1 → Mod(1,3)=1 → wire[1] = "20"
  //   (B) Modulo wrap: producers [10,20,30], Index=4 → Mod(4,3)=1 → wire[1] = "20"
  //   (C) ★euclidean-mod negative: producers [10,20,30], Index=-1 → Mod(-1,3)=2 → wire[2] = "30"
  //   (D) Wire-order proof: producers [30,10,20], Index=0 → wire[0] = "30" (wire-decl order, NOT sorted)
  //
  // RESIDENT sub-case (proves the resident MultiInput String wire feeds Input — mirror LEG 20):
  //   producers [7,8,9] → "7"/"8"/"9", Index=2 → Mod(2,3)=2 → wire[2] = "9".
  //
  // RED (injectBug): stringInjectBug() corrupts the REAL cook (PickString drops its last char); on the
  // resident path the SELECTED FloatToString producer also drops its last char before PickString reads
  // it. No want-inversion — the bug bites the actual cook/select path.
  //
  // Graph builder: PickString id=1 (terminal); FloatToString producers ids 2.. wired into Input (port
  // 1, the MultiInput). PickString ports: [0]=Selected(out), [1]=Input(multiInput), [2]=Index(Float).
  // Index rides params (NOT a String port → not in inputStrings).
  {
    auto makePickGraph = [](const std::vector<float>& producerVals, int index) {
      Graph g;
      Node pick; pick.id = 1; pick.type = "PickString";
      pick.params["Index"] = (float)index;
      g.nodes.push_back(pick);
      const int inputPin = pinId(1, /*Input portIndex=*/1);
      int connId = 800;
      int nextNode = 2;
      for (size_t i = 0; i < producerVals.size(); ++i) {
        Node p; p.id = nextNode++; p.type = "FloatToString";
        p.params["Value"] = producerVals[i];
        p.strParams["Format"] = "";  // integer-valued → plain decimal "10","20",...
        g.nodes.push_back(p);
        g.connections.push_back({connId++, pinId(p.id, /*Output port*/ 0), inputPin});
      }
      g.nextId = nextNode;
      return g;
    };
    // FLAT cook helper.
    auto cookPickFlat = [&](const std::vector<float>& vals, int index) -> std::string {
      Graph g = makePickGraph(vals, index);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };
    // RESIDENT cook helper (libFromGraph → buildEvalGraph → cookStringNodes → extStrOut[0]).
    auto cookPickResident = [&](const std::vector<float>& vals, int index) -> std::string {
      Graph g = makePickGraph(vals, index);
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString producers → extStrOut, THEN PickString selects
      const ResidentNode* n = rg.node("1");  // PickString resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Selected out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    };

    stringInjectBug() = injectBug;
    std::string gA = cookPickFlat({10.0f, 20.0f, 30.0f},  1);   // (A) "20"
    std::string gB = cookPickFlat({10.0f, 20.0f, 30.0f},  4);   // (B) "20" (Mod(4,3)=1)
    std::string gC = cookPickFlat({10.0f, 20.0f, 30.0f}, -1);   // (C) "30" (Mod(-1,3)=2)
    std::string gD = cookPickFlat({30.0f, 10.0f, 20.0f},  0);   // (D) "30" (wire-decl order)
    std::string gRes = cookPickResident({7.0f, 8.0f, 9.0f}, 2); // RES "9" (Mod(2,3)=2)
    stringInjectBug() = false;

    bool passA = (gA == "20");
    bool passB = (gB == "20");
    bool passC = (gC == "30");
    bool passD = (gD == "30");
    bool passRes = (gRes == "9");
    bool pass = passA && passB && passC && passD && passRes;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG31 PickString FLAT (A idx1)=\"%s\" want=\"20\"; "
                "(B idx4 mod)=\"%s\" want=\"20\"; (C idx-1 neg-mod)=\"%s\" want=\"30\"; "
                "(D wire-order)=\"%s\" want=\"30\"; RESIDENT(idx2)=\"%s\" want=\"9\" -> %s\n",
                gA.c_str(), gB.c_str(), gC.c_str(), gD.c_str(), gRes.c_str(),
                pass ? "PASS" : "FAIL");
  }

  // LEG 32 — SearchAndReplace FLAT + RESIDENT (TiXL string/search/SearchAndReplace.cs). OriginalString +
  // SearchPattern + Replace + UseRegex(bool) → String. inputStrings[0]=OriginalString, [1]=SearchPattern,
  // [2]=Replace; UseRegex rides params (bool dissolved to Float). The Replace value gets the literal
  // "\n"→newline translation (SearchAndReplace.cs:20).
  //
  // FLAT sub-cases (hand-derived against the .cs):
  //   (A) Literal replace-all: ("hello world","o","0",regex=false) → "hell0 w0rld" (both 'o' replaced)
  //   (B) ★guard empty-pattern: ("hello","","X",false) → pattern IsNullOrEmpty → Result=content="hello"
  //   (C) ★Replace "\n" translation: ("a,b",",","\\n",false) → replacement "\n" → "a\nb" (real newline)
  //   (D) Regex common-syntax (parity with C#): ("hello world","o","0",regex=true) → "hell0 w0rld"
  //   (E) Regex backref $N (identical C#/ECMAScript): ("John Smith","(\\w+) (\\w+)","$2 $1",true)
  //       → "Smith John"  (group reorder; $1/$2 replacement refs are byte-identical between engines)
  //   (F) ★FAITHFUL bad-pattern: ("hello","(","X",true) → "(" fails to compile → the .cs catch leaves
  //       Result.Value UNCHANGED. In our cook *c.output is never written → stays empty ""  ← expected ""
  //       (fork-searchreplace-bad-pattern-result-unchanged: matches the C# catch-no-assign behavior).
  //
  // RESIDENT sub-case (proves resident String wire feeds OriginalString; SearchPattern/Replace const):
  //   FloatToString(12321.0f,"") → "12321" wired into OriginalString; SearchPattern const "2",
  //   Replace const "9", regex=false → literal "2"→"9" → "19391".
  //
  // RED (injectBug): stringInjectBug() corrupts the REAL cook (drops the last char) for sub-cases that
  // WRITE a non-empty output (A/C/D/E and RESIDENT, plus B's "hello"); the (F) bad-pattern case yields
  // "" so the drop is a no-op there (its parity is the no-write behavior, not bug-bitten). On the
  // resident path the upstream FloatToString also drops its last char before SearchAndReplace runs.
  // No want-inversion — the bug bites the actual cook path on every non-empty leg.
  //
  // FLAT legs:
  {
    auto cookSAR = [&](const std::string& original, const std::string& pattern,
                       const std::string& replace, bool useRegex) -> std::string {
      Graph g;
      Node n; n.id = 1; n.type = "SearchAndReplace";
      n.strParams["OriginalString"] = original;   // unwired → strDef const
      n.strParams["SearchPattern"]  = pattern;    // unwired → strDef const
      n.strParams["Replace"]        = replace;    // unwired → strDef const
      n.params["UseRegex"]          = useRegex ? 1.0f : 0.0f;  // bool dissolved to Float
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      return out ? *out : std::string{};
    };

    stringInjectBug() = injectBug;
    std::string gA = cookSAR("hello world", "o", "0", false);             // (A) "hell0 w0rld"
    std::string gB = cookSAR("hello", "", "X", false);                    // (B) "hello" (empty-pattern guard)
    std::string gC = cookSAR("a,b", ",", "\\n", false);                   // (C) "a\nb" (\n translation)
    std::string gD = cookSAR("hello world", "o", "0", true);              // (D) "hell0 w0rld" (regex)
    std::string gE = cookSAR("John Smith", "(\\w+) (\\w+)", "$2 $1", true);  // (E) "Smith John" (backref)
    // (F) ★FAITHFUL bad-pattern: "(" fails to compile → the .cs catch leaves Result.Value UNCHANGED
    // (no assignment). A FRESH node's Result starts empty → the no-write leaves it "". We use a UNIQUE
    // node id (9) so its driver stringBuf slot is pristine (default-empty) — proving the no-write path
    // yields "" exactly as a fresh C# node would, rather than inheriting the reused id=1 buffer's stale
    // value. (Reusing id=1 would show the PRIOR result, an artifact of the per-pg stringBuf cache, not
    // the op semantics; the dedicated id isolates the faithful "unchanged from default" behavior.)
    std::string gF = [&]() -> std::string {
      Graph g;
      Node n; n.id = 9; n.type = "SearchAndReplace";
      n.strParams["OriginalString"] = "hello";
      n.strParams["SearchPattern"]  = "(";     // invalid regex (unbalanced paren)
      n.strParams["Replace"]        = "X";
      n.params["UseRegex"]          = 1.0f;
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/9);
      const std::string* out = pg.debugCookedString(9);
      return out ? *out : std::string{};
    }();
    stringInjectBug() = false;

    bool passA = (gA == "hell0 w0rld");
    bool passB = (gB == "hello");
    bool passC = (gC == "a\nb");
    bool passD = (gD == "hell0 w0rld");
    bool passE = (gE == "Smith John");
    bool passF = (gF == "");  // bad pattern → catch leaves Result unchanged → empty
    bool pass = passA && passB && passC && passD && passE && passF;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG32 SearchAndReplace FLAT (A literal)=\"%s\" want=\"hell0 w0rld\"; "
                "(B empty-pat)=\"%s\" want=\"hello\"; (C \\n-xlate)ok=%s; (D regex)=\"%s\" want=\"hell0 w0rld\"; "
                "(E backref)=\"%s\" want=\"Smith John\"; (F bad-pat unchanged)ok=%s -> %s\n",
                gA.c_str(), gB.c_str(), passC ? "OK" : "MISMATCH", gD.c_str(), gE.c_str(),
                passF ? "OK" : "MISMATCH", pass ? "PASS" : "FAIL");
  }
  // RESIDENT leg: FloatToString(12321,"") wired into OriginalString; SearchPattern "2", Replace "9",
  // regex=false → "19391".
  {
    // Graph: SearchAndReplace id=1 (terminal), FloatToString id=2 (String producer for OriginalString).
    // SearchAndReplace ports: [0]=Result(out), [1]=OriginalString(String in), [2]=SearchPattern(String
    //   in), [3]=Replace(String in), [4]=UseRegex(Float). FloatToString ports: [0]=Output(out).
    Graph g;
    Node sar; sar.id = 1; sar.type = "SearchAndReplace";
    sar.strParams["SearchPattern"] = "2";   // const path (wire-OR-const dual)
    sar.strParams["Replace"]       = "9";   // const path
    sar.params["UseRegex"]         = 0.0f;  // literal replace
    // OriginalString is WIRED (no strParams["OriginalString"] — the wire drives it on the resident path).
    g.nodes.push_back(sar);

    Node fts; fts.id = 2; fts.type = "FloatToString";
    fts.params["Value"] = 12321.0f; fts.strParams["Format"] = "";  // → "12321"
    g.nodes.push_back(fts);
    // FloatToString.Output (port 0) → SearchAndReplace.OriginalString (port 1).
    g.connections.push_back({900, pinId(2, /*out*/ 0), pinId(1, /*OriginalString*/ 1)});
    g.nextId = 3;

    stringInjectBug() = injectBug;
    std::string gotResident = [&]() -> std::string {
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut["2"][0], THEN SearchAndReplace reads it
      const ResidentNode* n = rg.node("1");  // SearchAndReplace resident path == flat node id "1"
      if (!n) return {};
      auto it = n->extStrOut.find(/*Result out port idx*/ 0);
      return it != n->extStrOut.end() ? it->second : std::string{};
    }();
    stringInjectBug() = false;

    bool pass = (gotResident == "19391");
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG32 SearchAndReplace RESIDENT FloatToString(12321)→Original, "
                "\"2\"→\"9\" =\"%s\" want=\"19391\" -> %s\n",
                gotResident.c_str(), pass ? "PASS" : "FAIL");
  }

  // LEG 33 — ★PickStringPart MULTI-OUTPUT (Sub-seam B): Fragments(String, port 0) + TotalCount(Int→Float,
  // port 1) emitted in ONE cook. TiXL string/logic/PickStringPart.cs. The load-bearing multi-output proof
  // is the TotalCount ASSERTION: a single-output bug (only the first String port written) leaves the
  // scalar channel empty → TotalCount reads its default (0 / -1) → FAIL. We assert BOTH the Fragments
  // string AND the TotalCount int, on BOTH the flat and resident legs.
  //
  // FLAT (Words mode, hand-derived against PickStringPart.cs):
  //   input "hello world foo", SplitInto=Words(1), FragmentStart=1, FragmentCount=2:
  //     Words split "[\s\.\;\,()`:]+" → ["hello","world","foo"]  (rawCount=3, no trailing empty → numChunks=3)
  //     GetFragment(start=1,count=2): idx0=(1)%3=1→"world"; idx1 += " " + (2)%3=2→"foo" → "world foo"
  //     TotalCount = _chunks.Length = 3  (raw chunk count)
  //   Flat readback: Fragments via debugCookedString(1); TotalCount via the node's outCache[1] (the flat
  //   scalar bridge channel the multi-output cook writes — port 1).
  //
  // RESIDENT (Characters mode, proves the resident String WIRE feeds InputText + multi-output fan):
  //   FloatToString(12345,"") → "12345" wired into InputText; SplitInto=Characters(0), FragmentStart=1,
  //   FragmentCount=3:
  //     Characters split Regex.Split("12345","") = ["","1","2","3","4","5",""]  (rawCount=7;
  //       trailing empty stripped → numChunks=6)
  //     GetFragment(start=1,count=3,delim ""): idx (1,2,3)%6 → chunks[1]"1"+chunks[2]"2"+chunks[3]"3" → "123"
  //     TotalCount = _chunks.Length = 7  (RAW — includes BOTH boundary empties; the fork-totalcount-raw-chunks)
  //   Resident readback: Fragments via extStrOut[0]; TotalCount via extOut[1] (the scalar fan channel).
  //
  // RED (injectBug): cookPickStringPart drops the last char of Fragments AND sets TotalCount = -999 (the
  // REAL cook path). Resident also corrupts the upstream FloatToString first. Both legs + both channels bite.
  {
    // --- FLAT leg ---
    stringInjectBug() = injectBug;
    std::string flatFrag;
    float flatCount = -1.0f;
    {
      Graph g;
      Node n; n.id = 1; n.type = "PickStringPart";
      n.strParams["InputText"] = "hello world foo";  // unwired → strDef const
      n.params["SplitInto"]     = 1.0f;  // Words
      n.params["FragmentStart"] = 1.0f;
      n.params["FragmentCount"] = 2.0f;
      g.nodes.push_back(n);
      EvaluationContext ctx{};
      ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
      const std::string* out = pg.debugCookedString(1);
      flatFrag = out ? *out : std::string{};
      // TotalCount (port 1) rode the flat scalar bridge → the graph node's outCache[1] (the multi-output
      // cook wrote it via const_cast). The golden owns `g`, so read it back directly — the genuine channel.
      if (const Node* gn = g.node(1)) flatCount = gn->outCache[1];
    }
    stringInjectBug() = false;
    bool flatFragOk  = (flatFrag == "world foo");
    bool flatCountOk = (std::fabs(flatCount - 3.0f) < 1e-5f);  // multi-output scalar proof (bug → -999)

    // --- RESIDENT leg (R-2 production path) ---
    stringInjectBug() = injectBug;
    std::string resFrag;
    float resCount = -1.0f;
    {
      // Graph: PickStringPart id=1 (terminal), FloatToString id=2 (String producer for InputText).
      // PickStringPart input ports: InputText is the FIRST String INPUT port. Its absolute spec index is
      // 2 (outputs [0]=Fragments,[1]=TotalCount; inputs [2]=InputText,[3]=SplitInto,...). InputText pin = 2.
      Graph g;
      Node n; n.id = 1; n.type = "PickStringPart";
      n.params["SplitInto"]     = 0.0f;  // Characters
      n.params["FragmentStart"] = 1.0f;
      n.params["FragmentCount"] = 3.0f;
      // InputText WIRED (no strParams — the resident String wire drives it).
      g.nodes.push_back(n);
      Node fts; fts.id = 2; fts.type = "FloatToString";
      fts.params["Value"] = 12345.0f; fts.strParams["Format"] = "";  // → "12345"
      g.nodes.push_back(fts);
      g.connections.push_back({1000, pinId(2, /*out*/ 0), pinId(1, /*InputText port idx*/ 2)});
      g.nextId = 3;

      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc;
      rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      cookStringNodes(rg, rc);  // cooks FloatToString → extStrOut, THEN PickStringPart fans Fragments+TotalCount
      const ResidentNode* nd = rg.node("1");
      if (nd) {
        auto it = nd->extStrOut.find(/*Fragments port idx*/ 0);
        resFrag = it != nd->extStrOut.end() ? it->second : std::string{};
        resCount = nd->extOut[/*TotalCount port idx*/ 1];  // the scalar fan channel
      }
    }
    stringInjectBug() = false;
    bool resFragOk  = (resFrag == "123");
    bool resCountOk = (std::fabs(resCount - 7.0f) < 1e-5f);  // RAW chunk count incl. both boundary empties

    bool pass = flatFragOk && flatCountOk && resFragOk && resCountOk;
    ok = ok && pass;
    std::printf("[selftest-stringrail] LEG33 PickStringPart FLAT Words frag=\"%s\" want=\"world foo\", "
                "TotalCount=%.1f want=3.0; RESIDENT Chars frag=\"%s\" want=\"123\", TotalCount=%.1f want=7.0 "
                "-> %s\n",
                flatFrag.c_str(), flatCount, resFrag.c_str(), resCount, pass ? "PASS" : "FAIL");
  }

  // LEG 34 (FilePathParts MULTI-OUTPUT) was EXTRACTED into string_rail_golden_subseama.cpp alongside the
  // Sub-seam A legs (LEG 35/36), to keep THIS file at-or-below its line-count cap when it gained the
  // runStringRailSubseamA call below. It still runs under --selftest-stringrail (via that call).
  //
  // LEG 39 (StringBuilderToString FLAT+RESIDENT) + LEG 40 (StringBuilder→StringBuilderToString chain)
  // were EXTRACTED into string_builder_golden.cpp by the same cap discipline, called unconditionally via
  // runStringBuilderGolden below.
  // NOTE: LEG 37/38 are PickStringFromList/ZipStringList in string_rail_golden_subseama.cpp.

  q->release();
  dev->release();
  pool->release();

  // Sub-seam A legs (LEG 34 FilePathParts + LEG 35 FloatListToString + LEG 36 JoinStringList) live in
  // string_rail_golden_subseama.cpp and run under THIS flag. Call UNCONDITIONALLY (NOT `ok && …` — that
  // short-circuits when an incumbent already failed in -bug mode, so the new legs would never run / bite)
  // then AND the result, so --selftest-stringrail covers LEG 20-40 (and -bug bites all of them).
  bool subseamAOk = runStringRailSubseamA(injectBug);
  ok = ok && subseamAOk;

  // LEG 39/40: StringBuilder + StringBuilderToString (string_builder_golden.cpp). Call UNCONDITIONALLY.
  bool builderOk = runStringBuilderGolden(injectBug);
  ok = ok && builderOk;

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL cook outputs -> ok is false -> return 1 (the tooth bites). No inversion.
  std::printf("[selftest-stringrail] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
