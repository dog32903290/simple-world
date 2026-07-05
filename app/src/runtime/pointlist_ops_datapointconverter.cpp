// DataPointConverter pointlist op — converts point data from CSV or JSON files into the host point-list
// currency, with custom column mapping for CSV/flat-JSON, and can export back to CSV or JSON.
//
// TiXL authority: external/tixl/Operators/Lib/point/io/DataPointConverter.cs (mirrored below):
//   ConvertFileAsync (:89-129): extension .json → LoadFromJson / .csv → LoadFromCsv, else error →
//     empty buffer. GetColumnMapping (:360-376): canonical key → the Csv*Mapping input's value
//     (defaults "Position X" … "Rotation W" … "Scale Z" / "F1").
//   LoadFromCsvAsync (:188-215): lines[0] = header, Split(',').Trim() → OrdinalIgnoreCase name→index;
//     rows Split(','); <=1 line → empty list.
//   ParseCsvRecord (:292-328): rotW column MISSING (index -1) → Quaternion.CreateFromYawPitchRoll(
//     rotY, rotX, rotZ) (RADIANS — the .cs strips '°' but never converts degrees); present → the raw
//     quaternion (rotX,rotY,rotZ,rotW). Scale defaults 1, position/F1 default 0. `new Point{...}` C#
//     struct-init → Color=(0,0,0,0) / F2=0 (same as DataPointImportExport — reproduced).
//   ParseJsonRecord (:253-290): the JSON records are FLAT objects; field keys = the SAME mapping values
//     (e.g. record["Position X"]); haveQuaternion = rotW key non-empty AND present in the record.
//   SaveToCsvAsync (:233-251): fixed header "Position X,…,Rotation X,Rotation Y,Rotation Z,F1,Scale …"
//     (EULER angles via ToEulerAngles, no Rotation W column); SaveToJsonAsync (:217-231) = the same
//     nested shape DataPointImportExport writes.
//   ToEulerAngles (:378-396): transcribed verbatim (atan2/asin/copysign forms below).
//   Value semantics (GetFloatFromJson/GetFloatFromCsv/ParseFloatString :330-358) → runtime/datapoint_json.
//
// NAMED FORKS (beyond the family-wide ones in pointlist_ops_datapointimportexport.cpp — the pointlist
// rail / level semantics for Convert+Export / no-hot-reload / flat-only String channel all apply here):
//   • fork-datapointconverter-header-first-wins: .NET ToDictionary THROWS on a duplicate CSV header
//     (→ the whole convert fails). sw keeps the FIRST occurrence and converts anyway. Observable only
//     for a malformed duplicate-header file.
//   • fork-datapointconverter-csv-lines: .NET ReadAllLines drops the final empty line after a trailing
//     newline; sw splits on '\n', strips '\r', and drops ONLY a trailing empty element — same rows.
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/datapoint_json.h"          // jsonParse / jsonFind / jsonToFloat / parseFloatString
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint

namespace sw {
namespace {

// Canonical mapping order == the inputStrings slots AFTER FilePath/ExportFilePath (spec String order):
// [2]=PosX [3]=PosY [4]=PosZ [5]=RotX [6]=RotY [7]=RotZ [8]=RotW [9]=ScaleX [10]=ScaleY [11]=ScaleZ [12]=F1.
enum MapSlot { M_PosX = 0, M_PosY, M_PosZ, M_RotX, M_RotY, M_RotZ, M_RotW, M_ScaleX, M_ScaleY,
               M_ScaleZ, M_F1, M_COUNT };
const char* const kMapDefaults[M_COUNT] = {"Position X", "Position Y", "Position Z", "Rotation X",
                                           "Rotation Y", "Rotation Z", "Rotation W", "Scale X",
                                           "Scale Y",    "Scale Z",    "F1"};

std::string trimCopy(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}

// System.Numerics Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll) — the exact reference formula
// (yaw about Y, pitch about X, roll about Z; half-angle products). Called by the .cs as (rotY, rotX, rotZ).
void quatFromYawPitchRoll(float yaw, float pitch, float roll, float out[4]) {
  float sr = std::sin(roll * 0.5f), cr = std::cos(roll * 0.5f);
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  out[0] = cy * sp * cr + sy * cp * sr;  // X
  out[1] = sy * cp * cr - cy * sp * sr;  // Y
  out[2] = cy * cp * sr - sy * sp * cr;  // Z
  out[3] = cy * cp * cr + sy * sp * sr;  // W
}

// ToEulerAngles (DataPointConverter.cs:378-396) — verbatim transcription (double math like the .cs).
void quatToEuler(float x, float y, float z, float w, float out[3]) {
  double sinrCosp = 2.0 * (w * x + y * z);
  double cosrCosp = 1.0 - 2.0 * (x * x + y * y);
  out[0] = (float)std::atan2(sinrCosp, cosrCosp);
  double sinp = 2.0 * (w * y - z * x);
  out[1] = std::fabs(sinp) >= 1.0 ? (float)std::copysign(M_PI / 2.0, sinp) : (float)std::asin(sinp);
  double sinyCosp = 2.0 * (w * z + x * y);
  double cosyCosp = 1.0 - 2.0 * (y * y + z * z);
  out[2] = (float)std::atan2(sinyCosp, cosyCosp);
}

// Split file text into CSV lines (fork-datapointconverter-csv-lines).
std::vector<std::string> csvLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string cur;
  for (char ch : text) {
    if (ch == '\n') { lines.push_back(cur); cur.clear(); }
    else if (ch != '\r') cur.push_back(ch);
  }
  if (!cur.empty()) lines.push_back(cur);  // trailing newline → no empty final row (ReadAllLines parity)
  return lines;
}

std::vector<std::string> splitCommas(const std::string& line) {
  std::vector<std::string> v;
  std::string cur;
  for (char ch : line) {
    if (ch == ',') { v.push_back(cur); cur.clear(); }
    else cur.push_back(ch);
  }
  v.push_back(cur);
  return v;
}

// GetFloatFromCsv (.cs:342-349).
float csvFloat(const std::vector<std::string>& values, int index, float def) {
  if (index < 0 || index >= (int)values.size()) return def;
  return parseFloatString(values[(size_t)index], def);
}

SwPoint makePoint(float px, float py, float pz, const float q[4], float sx, float sy, float sz, float f1) {
  SwPoint p{};  // C# struct-init zeros (Color / F2 stay 0) — see header.
  p.Position = {px, py, pz};
  p.Rotation = {q[0], q[1], q[2], q[3]};
  p.Scale = {sx, sy, sz};
  p.FX1 = f1;
  return p;
}

// LoadFromCsv (.cs:188-215 + ParseCsvRecord :292-328). mapping[i] = the mapped column NAME per slot.
bool loadFromCsv(const std::string& text, const std::string mapping[M_COUNT], std::vector<SwPoint>& out) {
  std::vector<std::string> lines = csvLines(text);
  out.clear();
  if (lines.size() <= 1) return true;  // header-only / empty → empty list (the .cs warns + returns)

  // header name → FIRST index (OrdinalIgnoreCase; fork-datapointconverter-header-first-wins).
  std::vector<std::string> headers = splitCommas(lines[0]);
  int idx[M_COUNT];
  for (int m = 0; m < M_COUNT; ++m) {
    idx[m] = -1;
    if (mapping[m].empty()) continue;
    for (size_t h = 0; h < headers.size(); ++h) {
      if (iequals(trimCopy(headers[h]), mapping[m])) { idx[m] = (int)h; break; }
    }
  }

  for (size_t i = 1; i < lines.size(); ++i) {
    std::vector<std::string> values = splitCommas(lines[i]);
    float rotX = csvFloat(values, idx[M_RotX], 0.0f);
    float rotY = csvFloat(values, idx[M_RotY], 0.0f);
    float rotZ = csvFloat(values, idx[M_RotZ], 0.0f);
    float q[4];
    if (idx[M_RotW] != -1) {  // quaternion columns present → raw quaternion (.cs:305-310)
      q[0] = rotX; q[1] = rotY; q[2] = rotZ; q[3] = csvFloat(values, idx[M_RotW], 0.0f);
    } else {                  // euler → CreateFromYawPitchRoll(rotY, rotX, rotZ) (.cs:313)
      quatFromYawPitchRoll(rotY, rotX, rotZ, q);
    }
    out.push_back(makePoint(csvFloat(values, idx[M_PosX], 0.0f), csvFloat(values, idx[M_PosY], 0.0f),
                            csvFloat(values, idx[M_PosZ], 0.0f), q,
                            csvFloat(values, idx[M_ScaleX], 1.0f), csvFloat(values, idx[M_ScaleY], 1.0f),
                            csvFloat(values, idx[M_ScaleZ], 1.0f), csvFloat(values, idx[M_F1], 0.0f)));
  }
  return true;
}

// LoadFromJson (.cs:173-186 + ParseJsonRecord :253-290): FLAT records keyed by the mapping values.
bool loadFromJsonFlat(const std::string& text, const std::string mapping[M_COUNT],
                      std::vector<SwPoint>& out) {
  JsonVal root;
  if (!jsonParse(text, root) || root.kind != JsonVal::Arr) return false;
  out.clear();
  out.reserve(root.arr.size());
  auto get = [&](const JsonVal& rec, int slot, float def) {
    if (mapping[slot].empty()) return def;  // IsNullOrWhiteSpace(key) → default (.cs:332)
    return jsonToFloat(jsonFind(rec, mapping[slot]), def);
  };
  for (const JsonVal& rec : root.arr) {
    float rotX = get(rec, M_RotX, 0.0f), rotY = get(rec, M_RotY, 0.0f), rotZ = get(rec, M_RotZ, 0.0f);
    // haveQuaternion = rotW key non-empty AND present in the record (.cs:267).
    bool haveQuat = !mapping[M_RotW].empty() && jsonFind(rec, mapping[M_RotW]) != nullptr;
    float q[4];
    if (haveQuat) { q[0] = rotX; q[1] = rotY; q[2] = rotZ; q[3] = get(rec, M_RotW, 0.0f); }
    else quatFromYawPitchRoll(rotY, rotX, rotZ, q);
    out.push_back(makePoint(get(rec, M_PosX, 0.0f), get(rec, M_PosY, 0.0f), get(rec, M_PosZ, 0.0f), q,
                            get(rec, M_ScaleX, 1.0f), get(rec, M_ScaleY, 1.0f), get(rec, M_ScaleZ, 1.0f),
                            get(rec, M_F1, 0.0f)));
  }
  return true;
}

std::string lowerExt(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return "";
  std::string e = path.substr(dot);
  for (char& ch : e) ch = (char)std::tolower((unsigned char)ch);
  return e;
}

// SaveToCsv (.cs:233-251): the fixed header + euler rows. %.9g float round-trip (family fork).
bool saveToCsv(const std::string& path, const std::vector<SwPoint>& pts) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  auto n = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.9g", (double)v); return std::string(b); };
  f << "Position X,Position Y,Position Z,Rotation X,Rotation Y,Rotation Z,F1,Scale X,Scale Y,Scale Z\n";
  for (const SwPoint& p : pts) {
    float e[3];
    quatToEuler(p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w, e);
    f << n(p.Position.x) << ',' << n(p.Position.y) << ',' << n(p.Position.z) << ',' << n(e[0]) << ','
      << n(e[1]) << ',' << n(e[2]) << ',' << n(p.FX1) << ',' << n(p.Scale.x) << ',' << n(p.Scale.y)
      << ',' << n(p.Scale.z) << '\n';
  }
  return true;
}

// SaveToJson (.cs:217-231): the SAME nested shape DataPointImportExport writes.
bool saveToJson(const std::string& path, const std::vector<SwPoint>& pts) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f.is_open()) return false;
  auto n = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%.9g", (double)v); return std::string(b); };
  f << "[\n";
  for (size_t i = 0; i < pts.size(); ++i) {
    const SwPoint& p = pts[i];
    f << "  { \"Position\": { \"X\": " << n(p.Position.x) << ", \"Y\": " << n(p.Position.y)
      << ", \"Z\": " << n(p.Position.z) << " }, \"Orientation\": { \"W\": " << n(p.Rotation.w)
      << ", \"X\": " << n(p.Rotation.x) << ", \"Y\": " << n(p.Rotation.y) << ", \"Z\": "
      << n(p.Rotation.z) << " }, \"Scale\": { \"X\": " << n(p.Scale.x) << ", \"Y\": " << n(p.Scale.y)
      << ", \"Z\": " << n(p.Scale.z) << " }, \"F1\": " << n(p.FX1) << " }"
      << (i + 1 < pts.size() ? "," : "") << "\n";
  }
  f << "]\n";
  return true;
}

void cookDataPointConverter(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // String inputs in spec String-port order: [0]=FilePath, [1]=ExportFilePath, [2..12]=the mappings.
  std::string filePath, exportPath;
  std::string mapping[M_COUNT];
  if (c.inputStrings) {
    if (c.inputStrings->size() > 0) filePath = (*c.inputStrings)[0];
    if (c.inputStrings->size() > 1) exportPath = (*c.inputStrings)[1];
    for (int m = 0; m < M_COUNT; ++m)
      mapping[m] = c.inputStrings->size() > (size_t)(2 + m) ? (*c.inputStrings)[(size_t)(2 + m)]
                                                            : kMapDefaults[m];
  } else {
    for (int m = 0; m < M_COUNT; ++m) mapping[m] = kMapDefaults[m];
  }

  // ------- CONVERT (level semantics — family fork) -------
  if (!filePath.empty()) {
    std::ifstream f(filePath, std::ios::binary);
    if (f.is_open()) {
      std::ostringstream ss;
      ss << f.rdbuf();
      std::string ext = lowerExt(filePath);
      if (ext == ".json") loadFromJsonFlat(ss.str(), mapping, *c.output);
      else if (ext == ".csv") loadFromCsv(ss.str(), mapping, *c.output);
      // other extension → NotSupportedException → catch → empty buffer (.cs:116-127): output stays empty.
    }
  }

  // ------- EXPORT (level bool; exports the CONVERTED points — .cs:140 requires a prior convert) -------
  if (pointListParam(c.params, "Export", 0.0f) >= 0.5f && !exportPath.empty() && !c.output->empty()) {
    std::string ext = lowerExt(exportPath);
    if (ext == ".json") saveToJson(exportPath, *c.output);
    else if (ext == ".csv") saveToCsv(exportPath, *c.output);
  }

  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. String ports in the cook's expected order (FilePath, ExportFilePath, 11 mappings —
// defaults faithful to the .cs input defaults). Convert kept for spec parity (level semantics fork).
static const PointListOp _reg_datapointconverter{
    {"DataPointConverter", "DataPointConverter",
     {{"PointBuffer", "PointBuffer", "PointList", false},
      {"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "points.json"},
      {"ExportFilePath", "ExportFilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "exported_points.json"},
      {"CsvPosXMapping", "CsvPosXMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Position X"},
      {"CsvPosYMapping", "CsvPosYMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Position Y"},
      {"CsvPosZMapping", "CsvPosZMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Position Z"},
      {"CsvRotXMapping", "CsvRotXMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Rotation X"},
      {"CsvRotYMapping", "CsvRotYMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Rotation Y"},
      {"CsvRotZMapping", "CsvRotZMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Rotation Z"},
      {"CsvRotWMapping", "CsvRotWMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, "Rotation W"},
      {"CsvScaleXMapping", "CsvScaleXMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, "Scale X"},
      {"CsvScaleYMapping", "CsvScaleYMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, "Scale Y"},
      {"CsvScaleZMapping", "CsvScaleZMapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, "Scale Z"},
      {"CsvF1Mapping", "CsvF1Mapping", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, "F1"},
      {"Convert", "Convert", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
      {"Export", "Export", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true}},
     /*evaluate=*/nullptr},
    cookDataPointConverter};

}  // namespace sw
