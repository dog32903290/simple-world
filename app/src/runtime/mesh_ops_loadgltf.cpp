// LoadGltf mesh op — load a glTF 2.0 / GLB into the MESH currency (SwVertex + SwTriIndex pair). The
// MINIMAL-VIABLE slice of TiXL LoadGltfScene: the FIRST mesh child's FIRST primitive, position + normal +
// texcoord + the computed index list. The full SceneSetup node tree, multi-primitive/multi-node flatten,
// material-channel texture decode, and the compute-shader RSMO combine (LoadGltfScene.cs:136-815) are the
// NEXT-BATTLE fork (named below) — this op lands the geometry the PBR draw needs NOW.
//
// TiXL authority: external/tixl/Operators/Lib/render/scene/LoadGltfScene.cs
//   GetMeshDataFromPrimitive (:902-1049) — the per-primitive vertex+index fill this cook mirrors 1:1:
//     POSITION accessor (:913-924); NORMAL → VectorT3.Up fallback (:927-931,:951); TEXCOORD_0 flip-Y
//     (:936-939,:954-961); ColorRgb = One, Selection = 1 (:962-963); Tangent=Right/Bitangent=ForwardLH
//     defaults (:952-953), then per-triangle TBN recompute (:987-1038 — DEFERRED here, defaults kept: the
//     PBR minimal path uses the vertex NORMAL directly, no normal-map tangent space).
//     Index loop (:976-984): GetTriangleIndices → Int3(a,b,c) VERBATIM (no winding reversal).
//
// PARSE: vendored cgltf (single-header, third_party/cgltf/cgltf.h — MIT, nanosvg precedent). CGLTF_IMPLEMENTATION
// is defined HERE (this is the one TU that pulls the impl). cgltf_load_buffers reads external .bin / GLB blobs.
//
// NAMED FORKS (the deferred SceneSetup half):
//   • fork-loadgltf-first-primitive: only the first mesh-bearing node's first primitive is loaded (TiXL's
//     MeshChildIndex picks among ALL flattened Dispatches; sw v1 = index 0). Multi-mesh scenes load their
//     first mesh only. The scene tree / transform hierarchy / per-node CombinedTransform → deferred.
//   • fork-loadgltf-material-dropped: the glTF material (baseColor/metallic/roughness/textures) is NOT
//     extracted — DrawMeshPbr reads the SCOPED SetMaterial instead. LoadGltfScene's Material output +
//     GetOrCreateMaterialDefinition (:525-671) → deferred with the scene tree.
//   • fork-loadgltf-tbn-deferred: per-triangle Tangent/Bitangent recompute (:987-1038) skipped; the minimal
//     PBR path uses the vertex normal (no normal-map tangent space). Tangent/Bitangent left at 0.
//   • fork-loadgltf-no-hot-reload: parsed data cached by path (leaf-static), same as LoadObj (no file watcher).
//   • fork-mesh-string-channel: Path rides MeshCookCtx::inputStrings[0] (LoadObj precedent).
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/mesh_op_registry.h"  // MeshOp registrar + MeshCookCtx + countStr twin + meshInjectBug
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

// Parsed data for one path (the parse cache entry — fork-loadgltf-no-hot-reload).
struct LoadedGltf {
  bool ok = false;
  std::vector<SwVertex> vertices;   // the first primitive's vertices (PbrVertex fields filled)
  std::vector<SwTriIndex> faces;    // the first primitive's triangle index list
};

// Read a cgltf accessor component into a float (handles the common float POSITION/NORMAL/TEXCOORD path).
// cgltf_accessor_read_float does the stride/normalization; we ask for `n` components into `out`.
bool readVec(const cgltf_accessor* acc, cgltf_size index, float* out, cgltf_size n) {
  return acc && cgltf_accessor_read_float(acc, index, out, n);
}

// Fill LoadedGltf from the first mesh-bearing primitive (GetMeshDataFromPrimitive, LoadGltfScene.cs:902-1049).
void parseFirstPrimitive(const cgltf_data* data, LoadedGltf& e) {
  if (!data) return;
  const cgltf_primitive* prim = nullptr;
  for (cgltf_size mi = 0; mi < data->meshes_count && !prim; ++mi) {
    const cgltf_mesh& m = data->meshes[mi];
    for (cgltf_size pi = 0; pi < m.primitives_count; ++pi) {
      if (m.primitives[pi].type == cgltf_primitive_type_triangles) { prim = &m.primitives[pi]; break; }
    }
  }
  if (!prim) return;

  // Find POSITION (required), NORMAL + TEXCOORD_0 (optional). GetMeshDataFromPrimitive:913-943.
  const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
  for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
    const cgltf_attribute& a = prim->attributes[ai];
    if (a.type == cgltf_attribute_type_position) pos = a.data;
    else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
    else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
  }
  if (!pos || pos->count == 0) return;  // GetMeshDataFromPrimitive:913-917 (no POSITION → fail)

  const cgltf_size vc = pos->count;
  e.vertices.assign(vc, SwVertex{});
  for (cgltf_size i = 0; i < vc; ++i) {
    SwVertex& v = e.vertices[i];
    float p[3] = {0, 0, 0};
    readVec(pos, i, p, 3);
    v.Position = {p[0], p[1], p[2]};
    // Normal → VectorT3.Up (0,1,0) fallback when no NORMAL accessor (LoadGltfScene.cs:951).
    float n[3] = {0.0f, 1.0f, 0.0f};
    if (nrm) readVec(nrm, i, n, 3);
    v.Normal = {n[0], n[1], n[2]};
    // Texcoord flip-Y (LoadGltfScene.cs:954-961: new Vector2(x, 1 - y)); Zero when no TEXCOORD_0.
    float t[2] = {0.0f, 0.0f};
    if (uv) { readVec(uv, i, t, 2); t[1] = 1.0f - t[1]; }
    v.Texcoord = {t[0], t[1]};
    v.Texcoord2 = {0.0f, 0.0f};
    v.Tangent = {0.0f, 0.0f, 0.0f};    // fork-loadgltf-tbn-deferred (PBR minimal path uses the normal)
    v.Bitangent = {0.0f, 0.0f, 0.0f};
    v.Selection = 1.0f;                 // LoadGltfScene.cs:963
    v.ColorRgb = {1.0f, 1.0f, 1.0f};    // Vector3.One (:962)
  }

  // Index loop (LoadGltfScene.cs:976-984): GetTriangleIndices → Int3(a,b,c). cgltf gives the flat index
  // accessor; we read it in triples. No winding reversal (TiXL emits the face order verbatim).
  if (prim->indices && prim->indices->count >= 3) {
    const cgltf_accessor* idx = prim->indices;
    const cgltf_size faceCount = idx->count / 3;
    e.faces.assign(faceCount, SwTriIndex{});
    for (cgltf_size f = 0; f < faceCount; ++f) {
      cgltf_uint a = 0, b = 0, cc = 0;
      cgltf_accessor_read_uint(idx, f * 3 + 0, &a, 1);
      cgltf_accessor_read_uint(idx, f * 3 + 1, &b, 1);
      cgltf_accessor_read_uint(idx, f * 3 + 2, &cc, 1);
      e.faces[f] = {(int32_t)a, (int32_t)b, (int32_t)cc};
    }
  } else {
    // Non-indexed primitive: sequential triples (GetTriangleIndices synthesizes 0,1,2,...).
    const cgltf_size faceCount = vc / 3;
    e.faces.assign(faceCount, SwTriIndex{});
    for (cgltf_size f = 0; f < faceCount; ++f)
      e.faces[f] = {(int32_t)(f * 3), (int32_t)(f * 3 + 1), (int32_t)(f * 3 + 2)};
  }
  e.ok = !e.vertices.empty() && !e.faces.empty();
}

std::map<std::string, LoadedGltf>& loadGltfCache() {
  static std::map<std::string, LoadedGltf> m;
  return m;
}

const LoadedGltf& loadGltfFor(const std::string& path) {
  auto& cache = loadGltfCache();
  auto it = cache.find(path);
  if (it != cache.end()) return it->second;
  LoadedGltf& e = cache[path];
  if (path.empty()) return e;  // no path → empty mesh (defined no-op)
  cgltf_options options{};
  cgltf_data* data = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &data) == cgltf_result_success) {
    cgltf_load_buffers(&options, data, path.c_str());  // resolve external .bin / GLB blobs
    parseFirstPrimitive(data, e);
    cgltf_free(data);
  }
  return e;
}

void loadGltfCount(const std::map<std::string, float>* /*params*/, const SwMeshView* /*inputs*/,
                   int /*inputCount*/, const std::vector<std::string>* inputStrings, uint32_t& vtx,
                   uint32_t& idx) {
  std::string path;
  if (inputStrings && !inputStrings->empty()) path = (*inputStrings)[0];
  const LoadedGltf& e = loadGltfFor(path);
  vtx = (uint32_t)e.vertices.size();
  idx = (uint32_t)e.faces.size();
}

void loadGltfCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  std::string path;
  if (c.inputStrings && !c.inputStrings->empty()) path = (*c.inputStrings)[0];
  const LoadedGltf& e = loadGltfFor(path);
  if (!e.ok || c.vertexCount == 0) return;

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  const uint32_t vc = (uint32_t)e.vertices.size();
  for (uint32_t i = 0; i < vc && i < c.vertexCount; ++i) verts[i] = e.vertices[i];
  const uint32_t fc = (uint32_t)e.faces.size();
  for (uint32_t f = 0; f < fc && f < c.indexCount; ++f) idx[f] = e.faces[f];

  // Test injection (golden RED): corrupt the first vertex position in the REAL output (family seam).
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec loadGltfSpec() {
  NodeSpec s;
  s.type = "LoadGltf";
  s.title = "Load Gltf";
  PortSpec path; path.id = "Path"; path.name = "Path"; path.dataType = "String"; path.isInput = true;
  path.strDef = "";  // no .t3 default that resolves headless; unresolvable → empty mesh (defined no-op)
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {path, out};
  return s;
}

const MeshOp g_loadGltfOp(loadGltfSpec(), loadGltfCount, loadGltfCook);

}  // namespace
}  // namespace sw
