// runtime/buffer_op_registry — the Buffer-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe, same as pointlist_op_registry.cpp / floatlist_op_registry.cpp).
#include "runtime/buffer_op_registry.h"

#include <cstdio>

namespace sw {

std::vector<NodeSpec>& bufferSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, BufferCookFn>& bufferCookFns() {
  static std::map<std::string, BufferCookFn> m;
  return m;
}

const BufferCookFn* findBufferOp(const std::string& type) {
  auto& m = bufferCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& bufferInjectBug() {
  static bool b = false;
  return b;
}

uint32_t& bufferUnresolvedMatrixSources() {
  static uint32_t n = 0;
  return n;
}

void noteUnresolvedMatrixSource() {
  bufferUnresolvedMatrixSources()++;
  static bool warned = false;  // warn-once (mirror pgdetail::warnCookDepthOnce)
  if (!warned) {
    warned = true;
    std::fprintf(stderr,
                 "[FloatsToBuffer] Vec4Params wired to a source with no matrix rows — the buffer's matrix "
                 "block is ZERO (missing producer? e.g. TransformMatrix.ResultInverted / GetMatrixVar).\n");
  }
}

BufferOp::BufferOp(NodeSpec spec, BufferCookFn cook) {
  bufferCookFns()[spec.type] = cook;
  bufferSpecSink().push_back(std::move(spec));
}

float bufferParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
