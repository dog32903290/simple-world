// LoadObj mesh op — load a Wavefront .OBJ into the MESH currency (SwVertex + SwTriIndex pair).
// SECOND consumer of the obj_parse seam (obj_parse.cpp/obj_parse_distinct.cpp — built FOR this op) and
// FIRST consumer of the mesh STRING channel (MeshCookCtx::inputStrings + the countStr string-aware
// count twin, mesh_op_registry.h).
//
// TiXL authority: external/tixl/Operators/Lib/mesh/generate/LoadObj.cs
//   TryCreateResource (:22-59): ObjMesh.TryLoadFromFile → UpdateVertexSorting(_vertexSorting) →
//     new MeshDataSet(mesh, _scaleFactor); GPU-cache by absolutePath when _useGpuCaching.
//   Update (:63-95): ClearGPUCache → cache.Clear() (:65-68); SortVertices/UseGPUCaching dirty or
//     |scale-_scaleFactor|>0.001 → re-create (:70-83).
//   MeshDataSet (:106-184) — the buffer fill this cook mirrors 1:1:
//     vertex loop (:121-145): sortedVertexIndex = SortedVertexIndices[vertexIndex];
//       reversedLookup[sortedVertexIndex] = vertexIndex;
//       PbrVertex{ Position = Positions[PositionIndex] * scaleFactor, Normal = Normals[NormalIndex],
//                  Tangent/Bitangent = VertexTangents/VertexBinormals[sortedVertexIndex],
//                  Texcoord = TexCoords[TextureCoordsIndex], Selection = 1,
//                  ColorRgb = Colors.Count > PositionIndex ? Colors[PositionIndex].rgb : One }  (:128-144)
//     index loop (:158-169): v index via GetVertexIndex(pos,n,t) hash → Int3(reversedLookup[each]).
//       NO winding reversal (unlike DelaunayMesh) — TiXL emits the OBJ face order verbatim.
//   UpdateVertexSorting (ObjMesh.cs:391-435): identity list; sort by Positions[distinct.PositionIndex]
//     axis per SortDirections (XForward..ZBackwards); Ignore (6) = no sort.
//   .t3 defaults: Path="Examples:meshes/marblePlayer.obj", SortVertices=6 (Ignore), ScaleFactor=1,
//     UseGPUCaching=false, ClearGPUCache=false.
//
// NAMED FORKS:
//   • fork-loadobj-parse-cache: TiXL wraps the mesh in Resource<MeshDataSet> (file watcher + optional
//     GPU-buffer cache). sw caches the PARSED+distinct data by path in a leaf-static map (no watcher —
//     the same fork-no-hot-reload as LoadObjAsPoints); ClearGPUCache=true clears it (LoadObj.cs:65-68
//     observable: next cook re-reads the file). UseGPUCaching shipped-unread (its TiXL meaning is the
//     GPU-buffer cache identity; sw rebuilds buffers each cook from the parse cache).
//   • fork-loadobj-stable-sort-tieorder: .NET List.Sort is unstable; sw std::stable_sort (same fork as
//     LoadObjAsPoints — observable only when vertices share the sort-axis value).
//   • fork-mesh-string-channel: Path rides MeshCookCtx::inputStrings[0] (flat: strParams override else
//     strDef; resident: flatten-resolved strInputs). Wired String sources are not cooked on the mesh
//     flow yet (const-only channel, mesh_op_registry.h).
//   • fork-loadobj-status-dropped: IStatusProvider warning surface (:212-220) is editor UI, not ported.
//   • Texcoord2 = 0 (TiXL leaves the unlisted PbrVertex fields default, :135-144).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/mesh_op_registry.h"  // MeshOp registrar + MeshCookCtx + countStr twin + meshInjectBug
#include "runtime/obj_parse.h"         // objParseFromFile + objBuildDistinctVertices (the seam built for this op)
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

// Parsed + distinct data for one path (the parse cache entry — fork-loadobj-parse-cache).
struct LoadedObj {
  bool ok = false;
  ObjMesh mesh;                      // texCoords MUTATED by the distinct build (fallback UVs)
  std::vector<ObjVertex> vertices;   // distinct (pos,normal,uv) triples, position-index re-sorted
  std::vector<ObjVec3> tangents;     // parallel to vertices
  std::vector<ObjVec3> binormals;    // parallel to vertices
  std::map<int64_t, int> indexByHash;  // GetVertexIndex twin (ObjMesh.cs:194-204 / :223 hash)
};

int64_t loadObjHash(int pos, int normal, int tex) {  // Vertex.GetHashForIndices (ObjMesh.cs:223)
  return ((int64_t)pos << 42) | ((int64_t)normal << 21) | (int64_t)tex;
}

std::map<std::string, LoadedObj>& loadObjCache() {
  static std::map<std::string, LoadedObj> m;
  return m;
}

const LoadedObj& loadObjFor(const std::string& path) {
  auto& cache = loadObjCache();
  auto it = cache.find(path);
  if (it != cache.end()) return it->second;
  LoadedObj& e = cache[path];
  e.ok = !path.empty() && objParseFromFile(path, e.mesh);
  if (e.ok) {
    objBuildDistinctVertices(e.mesh, e.vertices, e.tangents, e.binormals);
    for (size_t i = 0; i < e.vertices.size(); ++i) {
      const ObjVertex& v = e.vertices[i];
      e.indexByHash[loadObjHash(v.positionIndex, v.normalIndex, v.textureCoordsIndex)] = (int)i;
    }
    e.ok = !e.vertices.empty();  // TryLoadFromFile gate: DistinctDistinctVertices.Count != 0 (:125)
  }
  return e;
}

// UpdateVertexSorting (ObjMesh.cs:391-435): identity, then sort by the distinct vertex's POSITION
// axis. Sorting index: 0..5 = X/Y/Z Forward/Backwards, 6 = Ignore (no sort).
std::vector<int> loadObjSortedIndices(const LoadedObj& e, int sortMode) {
  std::vector<int> idx(e.vertices.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
  if (sortMode < 0 || sortMode > 5) return idx;  // Ignore (6) / out of range → identity
  const int axis = sortMode / 2;                 // 0/1→X, 2/3→Y, 4/5→Z
  const int dir = (sortMode % 2 == 0) ? 1 : -1;  // even=Forward, odd=Backwards
  auto axisOf = [&](int vi) {
    const ObjVec3& p = e.mesh.positions[(size_t)e.vertices[(size_t)vi].positionIndex];
    return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
  };
  // fork-loadobj-stable-sort-tieorder (header): .NET List.Sort is unstable; sw keeps ties stable.
  std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
    return dir > 0 ? (axisOf(a) < axisOf(b)) : (axisOf(a) > axisOf(b));
  });
  return idx;
}

void loadObjCounts(const std::map<std::string, float>* params,
                   const std::vector<std::string>* inputStrings, uint32_t& vtx, uint32_t& idx) {
  // ClearGPUCache=true → drop the parse cache BEFORE reading (LoadObj.cs:65-68; next read re-parses).
  if (cookMeshParam(params, "ClearGPUCache", 0.0f) > 0.5f) loadObjCache().clear();
  std::string path;
  if (inputStrings && !inputStrings->empty()) path = (*inputStrings)[0];
  const LoadedObj& e = loadObjFor(path);
  if (!e.ok) { vtx = 0; idx = 0; return; }  // TiXL: load failed → Data.Value untouched; sw: empty pair
  vtx = (uint32_t)e.vertices.size();
  idx = (uint32_t)e.mesh.faces.size();
}

void loadObjCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
                  int /*inputCount*/, const std::vector<std::string>* inputStrings, uint32_t& vtx,
                  uint32_t& idx) {
  loadObjCounts(params, inputStrings, vtx, idx);
}

void loadObjCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  std::string path;
  if (c.inputStrings && !c.inputStrings->empty()) path = (*c.inputStrings)[0];
  const LoadedObj& e = loadObjFor(path);
  if (!e.ok || c.vertexCount == 0) return;

  const float scale = cookMeshParam(c.params, "ScaleFactor", 1.0f);
  const int sortMode = (int)(cookMeshParam(c.params, "SortVertices", 6.0f) + 0.5f);  // .t3 default Ignore
  const std::vector<int> sorted = loadObjSortedIndices(e, sortMode);

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  const uint32_t vc = (uint32_t)e.vertices.size();
  const int colCount = (int)e.mesh.colors.size();
  std::vector<int> reversedLookup((size_t)vc, 0);

  // Vertex loop — LoadObj.cs:121-145, field-for-field.
  for (uint32_t vertexIndex = 0; vertexIndex < vc && vertexIndex < c.vertexCount; ++vertexIndex) {
    const int sortedVertexIndex = sorted[(size_t)vertexIndex];
    const ObjVertex& sv = e.vertices[(size_t)sortedVertexIndex];
    reversedLookup[(size_t)sortedVertexIndex] = (int)vertexIndex;

    const ObjVec3& p = e.mesh.positions[(size_t)sv.positionIndex];
    const ObjVec3& n = e.mesh.normals[(size_t)sv.normalIndex];
    const ObjVec3& t = e.tangents[(size_t)sortedVertexIndex];
    const ObjVec3& b = e.binormals[(size_t)sortedVertexIndex];
    const ObjVec2& uv = e.mesh.texCoords[(size_t)sv.textureCoordsIndex];
    // ColorRgb = Colors.Count > PositionIndex ? Colors[PositionIndex].rgb : One (:128-132).
    float cr = 1.0f, cg = 1.0f, cb = 1.0f;
    if (colCount > sv.positionIndex) {
      const ObjVec4& col = e.mesh.colors[(size_t)sv.positionIndex];
      cr = col.x; cg = col.y; cb = col.z;
    }

    SwVertex& v = verts[vertexIndex];
    v.Position = {p.x * scale, p.y * scale, p.z * scale};  // :137 Position * scaleFactor
    v.Normal = {n.x, n.y, n.z};
    v.Tangent = {t.x, t.y, t.z};
    v.Bitangent = {b.x, b.y, b.z};
    v.Texcoord = {uv.u, uv.v};
    v.Texcoord2 = {0.0f, 0.0f};  // unlisted PbrVertex field stays default (fork, header)
    v.Selection = 1.0f;          // :142
    v.ColorRgb = {cr, cg, cb};
  }

  // Index loop — LoadObj.cs:158-169: hash-lookup each face corner's distinct index, then remap through
  // reversedLookup. NO winding reversal (OBJ face order verbatim).
  const uint32_t fc = (uint32_t)e.mesh.faces.size();
  for (uint32_t faceIndex = 0; faceIndex < fc && faceIndex < c.indexCount; ++faceIndex) {
    const ObjFace& f = e.mesh.faces[(size_t)faceIndex];
    auto lookup = [&](int pos, int n, int t) {
      auto it = e.indexByHash.find(loadObjHash(pos, n, t));
      return it != e.indexByHash.end() ? it->second : 0;  // GetVertexIndex miss → -1 in TiXL would
                                                          // crash the array write; parse guarantees hits
    };
    const int v1 = lookup(f.v0, f.v0n, f.v0t);
    const int v2 = lookup(f.v1, f.v1n, f.v1t);
    const int v3 = lookup(f.v2, f.v2n, f.v2t);
    idx[faceIndex] = {reversedLookup[(size_t)v1], reversedLookup[(size_t)v2],
                      reversedLookup[(size_t)v3]};
  }

  // Test injection (golden RED): corrupt the first vertex position in the REAL output (family seam).
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec loadObjSpec() {
  NodeSpec s;
  s.type = "LoadObj";
  s.title = "Load Obj";
  PortSpec path; path.id = "Path"; path.name = "Path"; path.dataType = "String"; path.isInput = true;
  path.strDef = "Examples:meshes/marblePlayer.obj";  // .t3 default (verbatim; unresolvable → empty mesh)
  PortSpec gpu; gpu.id = "UseGPUCaching"; gpu.name = "UseGPUCaching"; gpu.dataType = "Float";
  gpu.isInput = true; gpu.def = 0.0f; gpu.minV = 0.0f; gpu.maxV = 1.0f; gpu.widget = Widget::Bool;
  gpu.pinless = true;  // shipped-unread (fork-loadobj-parse-cache, header)
  PortSpec clr; clr.id = "ClearGPUCache"; clr.name = "ClearGPUCache"; clr.dataType = "Float";
  clr.isInput = true; clr.def = 0.0f; clr.minV = 0.0f; clr.maxV = 1.0f; clr.widget = Widget::Bool;
  clr.pinless = true;
  PortSpec srt; srt.id = "SortVertices"; srt.name = "SortVertices"; srt.dataType = "Float";
  srt.isInput = true; srt.def = 6.0f; srt.minV = 0.0f; srt.maxV = 6.0f; srt.widget = Widget::Enum;
  srt.labels = {"XForward", "XBackwards", "YForward", "YBackwards", "ZForward", "ZBackwards", "Ignore"};
  PortSpec scl; scl.id = "ScaleFactor"; scl.name = "ScaleFactor"; scl.dataType = "Float";
  scl.isInput = true; scl.def = 1.0f; scl.minV = 0.0f; scl.maxV = 100.0f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {path, gpu, clr, srt, scl, out};
  return s;
}

const MeshOp g_loadObjOp(loadObjSpec(), loadObjCount, loadObjCook);

}  // namespace
}  // namespace sw
