// runtime/point_ops_image_filter_registry — the two sinks + the ImageFilterOp registrar ctor.
// Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS = src/runtime/point_ops*.cpp) picks it
// up with zero CMake edit. See image_filter_op_registry.h for the full self-registration contract.
#include "runtime/image_filter_op_registry.h"

#include <map>
#include <set>
#include <string>
#include <utility>

#include <Metal/Metal.hpp>  // cachedAssetTexture holds MTL::Texture* in a process-global cache

#include "runtime/point_graph.h"  // registerTexOp

#ifndef SW_ASSETS_DIR
#define SW_ASSETS_DIR ""  // set by CMake target_compile_definitions; "" => no asset resolution
#endif

namespace sw {

// Meyers singletons: function-local statics, constructed on first call (i.e. inside the first
// registrar ctor that runs during pre-main dynamic init). Safe across TUs — see header.
std::vector<NodeSpec>& imageFilterSpecSink() {
  static std::vector<NodeSpec> sink;
  return sink;
}

std::vector<std::pair<const char*, int (*)(bool)>>& imageFilterSelfTests() {
  static std::vector<std::pair<const char*, int (*)(bool)>> tests;
  return tests;
}

// Compute-leaf sinks — same Meyers-singleton lifetime as the two above (first touched inside the
// first ImageFilterComputeOp ctor that runs during pre-main dynamic init; consumed only after main).
std::set<std::string>& imageFilterComputeTypes() {
  static std::set<std::string> types;
  return types;
}

std::map<std::string, ImageFilterSizeFn>& imageFilterSizeFns() {
  static std::map<std::string, ImageFilterSizeFn> fns;
  return fns;
}

// Mipped-output sink — same Meyers-singleton lifetime as the sets above (first touched inside the
// first registrar ctor with mippedOutput=true during pre-main dynamic init; consumed only after main).
std::set<std::string>& imageFilterMippedOutputTypes() {
  static std::set<std::string> types;
  return types;
}

// Asset-texture sink (cookType -> "Lib:..." key) — same Meyers-singleton lifetime as above.
std::map<std::string, std::string>& imageFilterAssetTextures() {
  static std::map<std::string, std::string> m;
  return m;
}

// Asset key -> absolute repo path. Mirrors platform::resolveAssetPath but kept runtime-local (pure
// string, no platform include): strip the "Lib:" prefix, join under SW_ASSETS_DIR.
std::string resolveImageFilterAssetPath(const std::string& assetKey) {
  std::string dir = SW_ASSETS_DIR;
  if (dir.empty()) return "";
  std::string rel = assetKey;
  const std::string prefix = "Lib:";
  if (rel.rfind(prefix, 0) == 0) rel = rel.substr(prefix.size());
  return dir + "/" + rel;
}

// Asset-decode leaf seam: the runtime-held fn-ptr (the app / a selftest registers it).
static AssetTextureDecoder g_assetDecoder = nullptr;
void setAssetTextureDecoder(AssetTextureDecoder fn) { g_assetDecoder = fn; }
AssetTextureDecoder assetTextureDecoder() { return g_assetDecoder; }

// Process-global decoded-asset cache (device-global, like the PSO cache). Keyed by (assetKey, mipped).
static std::map<std::pair<std::string, bool>, MTL::Texture*>& assetCache() {
  static std::map<std::pair<std::string, bool>, MTL::Texture*> c;
  return c;
}

MTL::Texture* cachedAssetTexture(MTL::Device* dev, const std::string& assetKey, bool mipped) {
  if (!dev || !g_assetDecoder || assetKey.empty()) return nullptr;
  auto key = std::make_pair(assetKey, mipped);
  auto it = assetCache().find(key);
  if (it != assetCache().end()) return it->second;
  std::string abs = resolveImageFilterAssetPath(assetKey);
  MTL::Texture* tex = abs.empty() ? nullptr : g_assetDecoder(dev, abs.c_str(), mipped);
  assetCache()[key] = tex;  // memoize even nullptr (avoid re-decoding a missing/undecodable asset)
  return tex;
}

void clearImageFilterAssetCache() {
  for (auto& kv : assetCache())
    if (kv.second) kv.second->release();
  assetCache().clear();
}

ImageFilterOp::ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                             const char* selftestName, int (*selftest)(bool), bool mippedOutput) {
  registerTexOp(cookType, cook);
  imageFilterSpecSink().push_back(std::move(spec));
  if (mippedOutput) imageFilterMippedOutputTypes().insert(cookType);
  if (selftestName && selftest) imageFilterSelfTests().push_back({selftestName, selftest});
}

ImageFilterComputeOp::ImageFilterComputeOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                                           ImageFilterSizeFn sizeFn, const char* selftestName,
                                           int (*selftest)(bool), bool mippedOutput,
                                           const char* assetTextureKey) {
  registerTexOp(cookType, cook);
  imageFilterSpecSink().push_back(std::move(spec));
  imageFilterComputeTypes().insert(cookType);
  if (sizeFn) imageFilterSizeFns()[cookType] = sizeFn;
  if (mippedOutput) imageFilterMippedOutputTypes().insert(cookType);
  if (assetTextureKey) imageFilterAssetTextures()[cookType] = assetTextureKey;
  if (selftestName && selftest) imageFilterSelfTests().push_back({selftestName, selftest});
}

}  // namespace sw
