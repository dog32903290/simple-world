// runtime/point_graph_params — the resolved-param accessor SPINE for the flat cook (extracted out of
// point_graph.cpp to keep that file at-or-below its line-count cap, ARCHITECTURE.md rule 4 ratchet).
// Pure relocation, zero behaviour change: these are the SAME free functions (sw:: linkage), the SAME
// bodies, just in their own TU. The declarations stay in point_graph.h (cookParam/cookVecN/mapParam/…)
// and point_graph_internal.h (pgdetail::texOutputOrdinal), so every op-leaf caller is unchanged.
//
// What lives here:
//   cookParam / cookVecN  — the per-ctx-type overloads (Point/Cmd/Tex/Feedback) that just forward the
//                           ctx's `params` map into the local mapParam/mapVecN raw-map readers.
//   cookInputParam        — a Float param of the node feeding a buffer input (force ops).
//   texOutputOrdinal      — map an absolute spec port index to its ordinal among Texture2D OUTPUT ports
//                           (the feedback multi-tex-output helper; pgdetail namespace).
// NOTE: mapParam/mapVecN are file-local here (anon namespace), NOT public — point_graph.cpp keeps its
// own tiny local mapParam for its single non-ctx use (the per-input Count fallback), so the header API
// stays exactly cookParam/cookVecN/cookInputParam (unchanged, no header churn).
#include "runtime/point_graph.h"           // ctx types + cookParam/cookVecN/cookInputParam decls
#include "runtime/point_graph_internal.h"  // pgdetail::texOutputOrdinal decl

#include <map>
#include <string>

#include "runtime/graph.h"  // NodeSpec / PortSpec (texOutputOrdinal walks spec.ports)

namespace sw {
namespace {
float mapParam(const std::map<std::string, float>* m, const char* id, float def) {
  if (!m) return def;
  auto it = m->find(id);
  return it != m->end() ? it->second : def;
}
void mapVecN(const std::map<std::string, float>* m, const char* base, const float* fallback,
             int n, float* out) {
  static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
  for (int i = 0; i < n && i < 4; ++i)
    out[i] = mapParam(m, (std::string(base) + kSuffix[i]).c_str(), fallback[i]);
}
}  // namespace

float cookParam(const PointCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
float cookParam(const CmdCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
float cookParam(const TexCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
float cookParam(const FeedbackCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
void cookVecN(const PointCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
void cookVecN(const TexCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
void cookVecN(const CmdCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
float cookInputParam(const PointCookCtx& c, int input, const char* id, float def) {
  if (!c.inputParams || input < 0 || input >= c.inputCount) return def;
  return mapParam(c.inputParams[input], id, def);
}

// Multi-tex-output helper (the feedback seam): map an ABSOLUTE output port index (the spec port index
// recovered from a wire's fromPin via (pin-1)%100) to its ORDINAL among the node's Texture2D OUTPUT
// ports — 0 = first Texture2D output, 1 = second, … A single-output tex op (RenderTarget/Blur) has
// exactly one Texture2D output so this always returns 0 for it (byte-identical). KeepPreviousFrame has
// two (PreviousFrame then CurrentFrame in spec order). Returns 0 if the index is not an output / not a
// Texture2D port (safe default = the first output). Shared by both cook drivers via this header.
namespace pgdetail {
int texOutputOrdinal(const NodeSpec& spec, int absPortIndex) {
  if (absPortIndex < 0 || absPortIndex >= (int)spec.ports.size()) return 0;
  int ord = 0;
  for (int i = 0; i < (int)spec.ports.size(); ++i) {
    const PortSpec& p = spec.ports[i];
    if (p.isInput || p.dataType != "Texture2D") continue;
    if (i == absPortIndex) return ord;
    ++ord;
  }
  return 0;
}
}  // namespace pgdetail

}  // namespace sw
