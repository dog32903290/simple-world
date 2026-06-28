// runtime/obj_parse — pure-CPU Wavefront .OBJ text parser (the device-IO OBJ sub-lane core).
// TiXL authority: external/tixl/Core/Rendering/ObjMesh.cs (TryLoadFromFile @ :25). Mirrored 1:1.
//
// PLACEMENT: runtime zone, a pure leaf. Text → arrays only — NO platform/Cocoa/ModelIO, NO file I/O in
// the PARSE entry (objParseFromText); a convenience objParseFromFile reads the file with std::ifstream
// (raw path, binary, verbatim — the SAME posture as string_ops_readfile.cpp's readWholeFile) and then
// parses. Keeping the parse pure lets the golden assert it with zero filesystem dependency.
//
// WHAT IS MIRRORED (ObjMesh.cs, file:line):
//   • "v"  (:50-66)  → Positions; the OPTIONAL 7-token form `v x y z r g b` (lineEntries.Length == 7)
//                       adds a Color (r,g,b,1) — so Colors is parallel to Positions ONLY when every v has
//                       color (TiXL warns otherwise but still loads; we reproduce: Colors only grows on a
//                       7-token v line).
//   • "vt" (:67-72)  → TexCoords (u,v).
//   • "vn" (:74-80)  → Normals (x,y,z).
//   • "f"  (:82-102) → Faces. QUAD-ONLY triangulation: a 4-vertex face → TWO triangles (0,1,2)+(0,2,3).
//                       5+-gons are NOT handled (TiXL ignores tokens past index 4). 1-based → 0-based (-1).
//   • "l"  (:104)    → Lines (V0,V2), 1-based → 0-based (-1).
//   • SplitFaceIndices (:128): token "pos/uv/normal"; an EMPTY middle ("v//n") → uv index 0. A bare
//     "f a b c" with NO slashes (no normal index) THROWS in TiXL (v0Entries[2] out of range) → the whole
//     load FAILS. We REPRODUCE that trap: a face token with no normal field makes the parse fail (ok=false).
//
// NAMED FORKS (vs the CHECKED-OUT, sw-modified ObjMesh.cs — that file IS the parity reference):
//   • fork-objmesh-checked-out-resort: ObjMesh.cs (the checked-out copy) re-sorts distinct vertices by
//     position index so SortedVertexIndices[i]==i (:292-326). We mirror the CHECKED-OUT behaviour, not
//     stock Tooll3. (Distinct-vertex / TBN logic lives in obj_parse_distinct.cpp.)
//   • fork-objparse-throw-to-bool: ObjMesh.TryLoadFromFile catches exceptions → returns false; sw has no
//     exceptions in the hot path → a malformed token sets ok=false (the SAME observable: load failed).
//   • fork-objparse-pure-text: the PARSE takes a text buffer (no file watcher / Resource<>); the file read
//     is a separate convenience. TiXL's ResourceManager hot-reload is NOT ported (fork-no-hot-reload).
//   • fork-faces-must-have-normals: a face token lacking the normal index makes the load fail (faithful to
//     TiXL's throw on the missing v0Entries[2]).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// A parsed face = 3 vertices, each carrying (positionIndex, normalIndex, textureIndex) — 0-based.
// Mirrors ObjMesh.Face (ObjMesh.cs:144). A quad input produces TWO of these.
struct ObjFace {
  int v0, v0n, v0t;
  int v1, v1n, v1t;
  int v2, v2n, v2t;
};

// A parsed line segment = 2 position indices (0-based). Mirrors ObjMesh.Line (ObjMesh.cs:172).
struct ObjLine {
  int v0, v2;
};

// 3-float / 2-float / 4-float POD holders (host-only; no simd dependency in the pure parser).
struct ObjVec3 { float x, y, z; };
struct ObjVec2 { float u, v; };
struct ObjVec4 { float x, y, z, w; };

// The parsed mesh: RAW arrays in file order. LoadObjAsPoints consumes Positions / Colors DIRECTLY (it does
// NOT use distinct vertices — fork-loadobjaspoints-raw-positions). distinctVertices()/TBN (the later LoadObj
// consumer) live in obj_parse_distinct.cpp and read these raw arrays.
struct ObjMesh {
  std::vector<ObjVec3> positions;  // "v" x y z
  std::vector<ObjVec4> colors;     // "v" …r g b → (r,g,b,1); grows ONLY on 7-token v lines (parallel to
                                   // positions iff EVERY v had color, faithful to ObjMesh.cs:56-63)
  std::vector<ObjVec3> normals;    // "vn"
  std::vector<ObjVec2> texCoords;  // "vt"
  std::vector<ObjFace> faces;      // "f" (quad → 2 faces)
  std::vector<ObjLine> lines;      // "l"
};

// Parse OBJ TEXT (no file I/O) into `out`. Returns true on success. Faithful to ObjMesh.TryLoadFromFile:
//   • a malformed numeric token / a face token missing its normal index → false (fork-objparse-throw-to-bool
//     / fork-faces-must-have-normals — TiXL throws → TryLoadFromFile returns false).
//   • success requires a non-empty result: TiXL returns `DistinctDistinctVertices.Count != 0` (:125). We
//     mirror the observable: empty (no positions / no faces → no distinct vertices) → false.
bool objParseFromText(const std::string& text, ObjMesh& out);

// Convenience: read the file at `path` (raw path, binary, verbatim — string_ops_readfile.cpp posture) then
// objParseFromText. Empty/missing/unreadable path → false (mirrors TryLoadFromFile's IsNullOrEmpty / !Exists
// early-out @ :30). NOT pure (does file I/O) — kept out of objParseFromText so the parse stays hermetic.
bool objParseFromFile(const std::string& path, ObjMesh& out);

// --- distinct-vertex dedup + TBN (obj_parse_distinct.cpp; for the LATER LoadObj consumer, NOT LoadObjAsPoints) ---
// One distinct vertex = a unique (position, normal, texcoord)-index triple. Mirrors ObjMesh.Vertex (:206).
struct ObjVertex {
  int positionIndex, normalIndex, textureCoordsIndex;
};

// Build the distinct-vertex list (+ per-vertex tangent/binormal) from a parsed mesh, mirroring
// ObjMesh.InitializeVertices (:235) INCLUDING the checked-out position-index re-sort (:292-326,
// fork-objmesh-checked-out-resort). Outputs run parallel: vertices[i] ↔ tangents[i] ↔ binormals[i].
// (TBN per MeshUtils.CalcTBNSpace, MeshUtils.cs:5.) NOTE this MUTATES mesh.texCoords (TiXL appends
// fallback UVs for zero-UV faces, :261-271). Returns the distinct-vertex count.
size_t objBuildDistinctVertices(ObjMesh& mesh, std::vector<ObjVertex>& vertices,
                                std::vector<ObjVec3>& tangents, std::vector<ObjVec3>& binormals);

}  // namespace sw
