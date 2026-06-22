// runtime/point_graph_registry — the op registry / sink SPINE for the point graph (extracted out of
// point_graph.cpp to keep that file at-or-below its line-count cap, ARCHITECTURE.md rule 4 ratchet).
// Pure relocation, zero behaviour change: these are the SAME free functions (sw:: linkage), the SAME
// bodies, just in their own TU. The declarations stay in point_graph.h, so every op-leaf caller is
// unchanged.
//
// What lives here:
//   pgdetail::cookReg / drawReg / cmdReg / texReg  — the four Meyers-singleton op maps.
//   registerPointOp / registerDrawOp / registerCmdOp / registerTexOp  — the registrar entry points.
//   texOwnsOutputSink / registerTexOpOwnsOutput / texOpOwnsOutput  — OWN-TEXTURE sink.
//   texOwnFormatSink / registerTexOpOwnFormat / texOpOwnFormat     — OWN-TEXTURE FORMAT sink.
//   feedbackSink (anon-ns) / registerFeedbackOp / isFeedbackOp /   — FEEDBACK sink.
//   feedbackNeedsPair / feedbackPairFormat / findFeedbackOp
#include "runtime/point_graph.h"           // fn-type aliases (PointCookFn, PointDrawFn, …)
#include "runtime/point_graph_internal.h"  // pgdetail::OpReg struct + registry decls

#include <map>
#include <set>
#include <string>

namespace sw {

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::drawReg;
using pgdetail::texReg;

namespace pgdetail {
std::map<std::string, OpReg>& cookReg() {
  static std::map<std::string, OpReg> m;
  return m;
}
std::map<std::string, PointDrawFn>& drawReg() {
  static std::map<std::string, PointDrawFn> m;
  return m;
}
std::map<std::string, PointCmdFn>& cmdReg() {
  static std::map<std::string, PointCmdFn> m;
  return m;
}
std::map<std::string, PointTexFn>& texReg() {
  static std::map<std::string, PointTexFn> m;
  return m;
}
}  // namespace pgdetail

void registerPointOp(const std::string& type, PointCookFn cook, PointStateNewFn stNew,
                     PointStateFreeFn stFree, PointCountFn countTransform,
                     bool countFromFirstPointsInput, bool countFromMeshVtx) {
  cookReg()[type] = pgdetail::OpReg{cook,
                                    stNew,
                                    stFree,
                                    countTransform,
                                    countFromFirstPointsInput,
                                    countFromMeshVtx};
}
void registerDrawOp(const std::string& type, PointDrawFn draw) { drawReg()[type] = draw; }
void registerCmdOp(const std::string& type, PointCmdFn cmd) { cmdReg()[type] = cmd; }
void registerTexOp(const std::string& type, PointTexFn tex) { texReg()[type] = tex; }

// OWN-TEXTURE sink (Slice B tex-output fork). Meyers singleton; populated by ValuesToTexture's
// registrar during pre-main dynamic init, read live by the tex-walker. A type in this set takes the
// ownTexHost/W/H staging path (ensureOwnedTex, R32Float) instead of the ensureTex RGBA8 output.
static std::set<std::string>& texOwnsOutputSink() {
  static std::set<std::string> s;
  return s;
}
void registerTexOpOwnsOutput(const std::string& type) { texOwnsOutputSink().insert(type); }
bool texOpOwnsOutput(const std::string& type) { return texOwnsOutputSink().count(type) != 0; }

// OWN-TEXTURE FORMAT sink (floats/texel for an own-tex op). Meyers singleton; populated by an own-tex
// registrar (GradientsToTexture → 4) at pre-main init, read live by the tex-walker. An own-tex type
// NOT in this map defaults to 1 (R32Float) — byte-identical for ValuesToTexture (never registers here).
static std::map<std::string, int>& texOwnFormatSink() {
  static std::map<std::string, int> m;
  return m;
}
void registerTexOpOwnFormat(const std::string& type, int floatsPerTexel) {
  texOwnFormatSink()[type] = floatsPerTexel;
}
int texOpOwnFormat(const std::string& type) {
  auto it = texOwnFormatSink().find(type);
  return it != texOwnFormatSink().end() ? it->second : 1;  // default 1 float/texel = R32Float
}

// FEEDBACK / multi-tex-output sink (cross-frame ping-pong flow = KeepPreviousFrame / SwapTextures).
// A type here routes its Texture2D inputs/outputs through the cook driver's feedback path (multi-output
// aware + optional cross-frame pair) instead of the single-output tex path. Registered explicitly at
// app init (registerBuiltinPointOps), read live by both cook drivers. A type NOT here = a normal tex op.
namespace {
struct FeedbackReg {
  PointFeedbackFn fn = nullptr;
  bool needsPair = false;
  uint32_t pairFormat = 0;  // MTL::PixelFormat raw (used only when needsPair)
};
std::map<std::string, FeedbackReg>& feedbackSink() {
  static std::map<std::string, FeedbackReg> m;
  return m;
}
}  // namespace
void registerFeedbackOp(const std::string& type, PointFeedbackFn fn, bool needsCrossFramePair,
                        uint32_t pairFormat) {
  feedbackSink()[type] = FeedbackReg{fn, needsCrossFramePair, pairFormat};
}
bool isFeedbackOp(const std::string& type) { return feedbackSink().count(type) != 0; }
bool feedbackNeedsPair(const std::string& type) {
  auto it = feedbackSink().find(type);
  return it != feedbackSink().end() && it->second.needsPair;
}
uint32_t feedbackPairFormat(const std::string& type) {
  auto it = feedbackSink().find(type);
  return it != feedbackSink().end() ? it->second.pairFormat : 0u;
}
PointFeedbackFn findFeedbackOp(const std::string& type) {
  auto it = feedbackSink().find(type);
  return it != feedbackSink().end() ? it->second.fn : nullptr;
}

}  // namespace sw
