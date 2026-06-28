// runtime/obj_parse — pure-CPU .OBJ text parser. TiXL authority: Core/Rendering/ObjMesh.cs (mirrored 1:1).
// See obj_parse.h for the full fork ledger. This file = the PARSE (text → raw arrays) + the file-read
// convenience. Distinct-vertex dedup + TBN live in obj_parse_distinct.cpp (split per ARCHITECTURE rule 4).
#include "runtime/obj_parse.h"

#include <cstdlib>   // strtof / strtol
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

namespace sw {

namespace {

// Split `s` on a single delimiter, KEEPING empty fields — the exact semantics of C#'s string.Split(char):
// "1//3".Split('/') == ["1","","3"]; "v 1 2 3".Split(' ') == ["v","1","2","3"]. ObjMesh.cs relies on this
// (an empty middle UV field "v//n" → texture index 0; a missing third field → IndexOutOfRange → throw).
std::vector<std::string> splitKeepEmpty(const std::string& s, char delim) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t pos = s.find(delim, start);
    if (pos == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

// float.Parse(token, InvariantCulture). strtof parses a standard "-1.5e2" decimal (the OBJ numeric form).
// `ok` is set false if the token is empty or has no parseable number (mirrors float.Parse's FormatException
// → the TryLoadFromFile catch → return false). FAITHFUL: .NET float.Parse THROWS on trailing garbage
// ("1.5x" → FormatException), so we reject it too — anything after the number that is not whitespace makes
// the load fail (tokens are already space-split, so trailing whitespace is not expected, but .NET's parse
// trims surrounding whitespace, so we allow trailing spaces for exact parity).
float parseFloat(const std::string& tok, bool& ok) {
  if (tok.empty()) { ok = false; return 0.0f; }
  const char* b = tok.c_str();
  char* end = nullptr;
  float v = std::strtof(b, &end);
  if (end == b) { ok = false; return 0.0f; }  // no digits consumed → FormatException → load fails
  while (*end == ' ' || *end == '\t') ++end;  // .NET trims surrounding whitespace
  if (*end != '\0') { ok = false; return 0.0f; }  // trailing garbage → FormatException → load fails
  return v;
}

// int.Parse(token, InvariantCulture) - 1  (1-based OBJ index → 0-based). `ok` false on a non-numeric token.
int parseIndexMinus1(const std::string& tok, bool& ok) {
  if (tok.empty()) { ok = false; return 0; }
  const char* b = tok.c_str();
  char* end = nullptr;
  long v = std::strtol(b, &end, 10);
  if (end == b) { ok = false; return 0; }
  return (int)v - 1;  // 1-based → 0-based
}

// SplitFaceIndices (ObjMesh.cs:128): token "pos/uv/normal" → 0-based (pos, tex, normal). An empty middle
// ("v//n") → textureIndex 0. A token with NO normal field (e.g. bare "a", or "a/b") accesses v0Entries[2]
// → IndexOutOfRange in TiXL → throw → load fails. We REPRODUCE that: fewer than 3 slash-fields → ok=false
// (fork-faces-must-have-normals).
void splitFaceIndices(const std::string& tokRaw, int& posIdx, int& texIdx, int& normIdx, bool& ok) {
  std::vector<std::string> e = splitKeepEmpty(tokRaw, '/');
  if (e.size() < 3) { ok = false; posIdx = texIdx = normIdx = 0; return; }  // missing normal → TiXL throw
  posIdx = parseIndexMinus1(e[0], ok);
  if (e[1].empty()) {
    texIdx = 0;                                  // "v//n" → uv index 0 (ObjMesh.cs:132-136)
    normIdx = parseIndexMinus1(e[2], ok);
  } else {
    texIdx = parseIndexMinus1(e[1], ok);
    normIdx = parseIndexMinus1(e[2], ok);
  }
}

}  // namespace

bool objParseFromText(const std::string& text, ObjMesh& out) {
  out = ObjMesh{};  // start clean (mesh = new ObjMesh())

  bool ok = true;
  std::istringstream stream(text);
  std::string line;
  // ReadLine() strips the trailing '\n'; a trailing '\r' (CRLF files) would otherwise ride into the last
  // token. std::getline keeps the '\r' on Windows-newline content → strip it so tokens stay clean.
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::vector<std::string> t = splitKeepEmpty(line, ' ');
    if (t.empty()) continue;
    const std::string& tag = t[0];

    if (tag == "v") {
      // v x y z [r g b]  (ObjMesh.cs:50-66). lineEntries[1..3] = position; length==7 → +color.
      if (t.size() < 4) { ok = false; break; }
      float x = parseFloat(t[1], ok), y = parseFloat(t[2], ok), z = parseFloat(t[3], ok);
      out.positions.push_back({x, y, z});
      if (t.size() == 7) {  // vertexIncludesColor (EXACT length 7, like TiXL)
        float r = parseFloat(t[4], ok), g = parseFloat(t[5], ok), b = parseFloat(t[6], ok);
        out.colors.push_back({r, g, b, 1.0f});  // (r,g,b,1)
      }
    } else if (tag == "vt") {
      if (t.size() < 3) { ok = false; break; }
      out.texCoords.push_back({parseFloat(t[1], ok), parseFloat(t[2], ok)});
    } else if (tag == "vn") {
      if (t.size() < 4) { ok = false; break; }
      out.normals.push_back({parseFloat(t[1], ok), parseFloat(t[2], ok), parseFloat(t[3], ok)});
    } else if (tag == "f") {
      // f v0 v1 v2 [v3]  (ObjMesh.cs:82-102). Triangle from tokens 1..3; a 4th token (QUAD) → a SECOND
      // triangle (0,2,3). 5+-gons: tokens past index 4 are IGNORED (faithful to TiXL).
      if (t.size() < 4) { ok = false; break; }
      int v0V, v0T, v0N, v1V, v1T, v1N, v2V, v2T, v2N;
      splitFaceIndices(t[1], v0V, v0T, v0N, ok);
      splitFaceIndices(t[2], v1V, v1T, v1N, ok);
      splitFaceIndices(t[3], v2V, v2T, v2N, ok);
      if (!ok) break;
      out.faces.push_back({v0V, v0N, v0T, v1V, v1N, v1T, v2V, v2N, v2T});
      if (t.size() > 4) {  // QUAD → triangle (0,2,3)
        int v3V, v3T, v3N;
        splitFaceIndices(t[4], v3V, v3T, v3N, ok);
        if (!ok) break;
        out.faces.push_back({v0V, v0N, v0T, v2V, v2N, v2T, v3V, v3N, v3T});
      }
    } else if (tag == "l") {
      // l v0 v1  (ObjMesh.cs:104). Two 1-based indices → 0-based (V0, V2).
      if (t.size() < 3) { ok = false; break; }
      int a = parseIndexMinus1(t[1], ok), b = parseIndexMinus1(t[2], ok);
      out.lines.push_back({a, b});
    }
    // else: comment '#', 'o', 'g', 's', 'mtllib', 'usemtl', blank … ignored (the switch has no other case).
    if (!ok) break;
  }

  if (!ok) { out = ObjMesh{}; return false; }  // TryLoadFromFile catch → mesh discarded, return false

  // BOUNDS GUARD (fork-objparse-bounds-fail-to-load): every face/line index must address an existing
  // position / normal / texcoord. TiXL has no explicit check — it derefs Positions[face.V0] etc. inside
  // TryLoadFromFile (InitializeVertices, ObjMesh.cs:277-290), so an out-of-range index throws
  // IndexOutOfRangeException → the TryLoadFromFile catch → returns false (the LOAD FAILS). sw has no
  // exceptions in the hot path, and the raw-position consumers (LoadObjAsPoints/LoadObjEdges) index
  // mesh.Positions[face.V*] directly while the distinct path (obj_parse_distinct.cpp) indexes
  // positions/normals/texCoords — ALL of which would segfault on an out-of-range index. So we validate up
  // front and FAIL the load (NOT clamp — clamping would diverge from TiXL's load-fails behaviour).
  const int posN = (int)out.positions.size();
  const int normN = (int)out.normals.size();
  const int texN = (int)out.texCoords.size();
  auto inRange = [](int idx, int n) { return idx >= 0 && idx < n; };
  for (const ObjFace& f : out.faces) {
    // texCoords: a "v//n" face left texIdx=0; if the file has NO vt lines that index is still out of
    // range. TiXL's InitializeVertices ensures >=1 texcoord (ObjMesh.cs:235) so texIdx 0 is always valid
    // there — mirror that allowance: tex index 0 is OK even with zero vt (the distinct path appends one).
    bool texOk = (f.v0t == 0 || inRange(f.v0t, texN)) && (f.v1t == 0 || inRange(f.v1t, texN)) &&
                 (f.v2t == 0 || inRange(f.v2t, texN));
    if (!inRange(f.v0, posN) || !inRange(f.v1, posN) || !inRange(f.v2, posN) ||
        !inRange(f.v0n, normN) || !inRange(f.v1n, normN) || !inRange(f.v2n, normN) || !texOk) {
      out = ObjMesh{};
      return false;  // out-of-range face index → load fails (IndexOutOfRange → catch → false)
    }
  }
  for (const ObjLine& l : out.lines) {  // "l" indices also deref Positions in TiXL's line path
    if (!inRange(l.v0, posN) || !inRange(l.v2, posN)) { out = ObjMesh{}; return false; }
  }

  // TiXL's success = DistinctDistinctVertices.Count != 0 (ObjMesh.cs:125). DistinctDistinctVertices is
  // built by InitializeVertices, which iterates FACES only (ObjMesh.cs:277-290) — so a mesh with NO faces
  // (e.g. a point cloud, or a lines-only file) yields ZERO distinct vertices → TryLoadFromFile returns
  // FALSE even though Positions/Lines were parsed. We mirror that observable: success REQUIRES at least one
  // face. (fork-objparse-success-needs-faces — faithful to TiXL's gate; LoadObjAsPoints reads RAW positions,
  // but the parse-success contract still follows ObjMesh.cs:125, so a faceless .obj fails to load.)
  if (out.faces.empty()) return false;
  return true;
}

bool objParseFromFile(const std::string& path, ObjMesh& out) {
  out = ObjMesh{};
  if (path.empty()) return false;                       // IsNullOrEmpty → false (ObjMesh.cs:30)
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;                      // !Exists / open failure → false
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) return false;
  return objParseFromText(ss.str(), out);
}

}  // namespace sw
