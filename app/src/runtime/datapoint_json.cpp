// runtime/datapoint_json — minimal JSON reader implementation (see header for scope + TiXL authority).
#include "runtime/datapoint_json.h"

#include <cctype>
#include <cstdlib>

namespace sw {
namespace {

struct Cursor {
  const char* p;
  const char* end;
};

void skipWs(Cursor& c) {
  while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\n' || *c.p == '\r')) ++c.p;
}

bool parseValue(Cursor& c, JsonVal& out, int depth);

// Parse a JSON string literal (opening quote already peeked). Handles the standard escapes; \uXXXX is
// decoded only for the ASCII range (the point-data keys/values this rail consumes are ASCII; a
// non-ASCII \u escape is replaced with '?' — named micro-fork, unreachable for the two ops' data).
bool parseString(Cursor& c, std::string& out) {
  if (c.p >= c.end || *c.p != '"') return false;
  ++c.p;
  out.clear();
  while (c.p < c.end) {
    char ch = *c.p++;
    if (ch == '"') return true;
    if (ch == '\\') {
      if (c.p >= c.end) return false;
      char e = *c.p++;
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          if (c.end - c.p < 4) return false;
          unsigned v = 0;
          for (int i = 0; i < 4; ++i) {
            char h = *c.p++;
            v <<= 4;
            if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
            else return false;
          }
          out.push_back(v < 128 ? (char)v : '?');
          break;
        }
        default: return false;
      }
    } else {
      out.push_back(ch);
    }
  }
  return false;  // unterminated
}

bool parseObject(Cursor& c, JsonVal& out, int depth) {
  ++c.p;  // consume '{'
  out.kind = JsonVal::Obj;
  skipWs(c);
  if (c.p < c.end && *c.p == '}') { ++c.p; return true; }
  while (c.p < c.end) {
    skipWs(c);
    std::string key;
    if (!parseString(c, key)) return false;
    skipWs(c);
    if (c.p >= c.end || *c.p != ':') return false;
    ++c.p;
    JsonVal v;
    if (!parseValue(c, v, depth + 1)) return false;
    out.obj.emplace_back(std::move(key), std::move(v));
    skipWs(c);
    if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
    if (c.p < c.end && *c.p == '}') { ++c.p; return true; }
    return false;
  }
  return false;
}

bool parseArray(Cursor& c, JsonVal& out, int depth) {
  ++c.p;  // consume '['
  out.kind = JsonVal::Arr;
  skipWs(c);
  if (c.p < c.end && *c.p == ']') { ++c.p; return true; }
  while (c.p < c.end) {
    JsonVal v;
    if (!parseValue(c, v, depth + 1)) return false;
    out.arr.push_back(std::move(v));
    skipWs(c);
    if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
    if (c.p < c.end && *c.p == ']') { ++c.p; return true; }
    return false;
  }
  return false;
}

bool parseValue(Cursor& c, JsonVal& out, int depth) {
  if (depth > 64) return false;  // depth guard (the point-data shapes are 2 levels deep)
  skipWs(c);
  if (c.p >= c.end) return false;
  char ch = *c.p;
  if (ch == '{') return parseObject(c, out, depth);
  if (ch == '[') return parseArray(c, out, depth);
  if (ch == '"') { out.kind = JsonVal::Str; return parseString(c, out.str); }
  if (ch == 't') {
    if (c.end - c.p >= 4 && c.p[1] == 'r' && c.p[2] == 'u' && c.p[3] == 'e') {
      c.p += 4; out.kind = JsonVal::Bool; out.boolean = true; return true;
    }
    return false;
  }
  if (ch == 'f') {
    if (c.end - c.p >= 5 && c.p[1] == 'a' && c.p[2] == 'l' && c.p[3] == 's' && c.p[4] == 'e') {
      c.p += 5; out.kind = JsonVal::Bool; out.boolean = false; return true;
    }
    return false;
  }
  if (ch == 'n') {
    if (c.end - c.p >= 4 && c.p[1] == 'u' && c.p[2] == 'l' && c.p[3] == 'l') {
      c.p += 4; out.kind = JsonVal::Null; return true;
    }
    return false;
  }
  // number
  char* numEnd = nullptr;
  double d = std::strtod(c.p, &numEnd);
  if (numEnd == c.p || numEnd > c.end) return false;
  c.p = numEnd;
  out.kind = JsonVal::Num;
  out.num = d;
  return true;
}

}  // namespace

bool jsonParse(const std::string& text, JsonVal& out) {
  Cursor c{text.data(), text.data() + text.size()};
  if (!parseValue(c, out, 0)) return false;
  skipWs(c);
  return c.p == c.end;  // trailing garbage = malformed (mirrors a .NET deserialize failure)
}

const JsonVal* jsonFind(const JsonVal& v, const std::string& key) {
  if (v.kind != JsonVal::Obj) return nullptr;
  for (const auto& kv : v.obj)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

float parseFloatString(const std::string& raw, float def) {
  // ParseFloatString (DataPointImportExport.cs:339-346): Replace("m","").Replace("°","").Trim().
  // The degree sign in UTF-8 is the 2-byte sequence 0xC2 0xB0 — strip it as a unit.
  std::string cleaned;
  cleaned.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    unsigned char ch = (unsigned char)raw[i];
    if (ch == 'm') continue;
    if (ch == 0xC2 && i + 1 < raw.size() && (unsigned char)raw[i + 1] == 0xB0) { ++i; continue; }
    cleaned.push_back((char)ch);
  }
  size_t b = 0, e = cleaned.size();
  while (b < e && std::isspace((unsigned char)cleaned[b])) ++b;
  while (e > b && std::isspace((unsigned char)cleaned[e - 1])) --e;
  if (b == e) return def;
  std::string t = cleaned.substr(b, e - b);
  char* end = nullptr;
  double d = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0') return def;  // float.TryParse: whole-string or fail
  return (float)d;
}

float jsonToFloat(const JsonVal* v, float def) {
  if (!v) return def;
  if (v->kind == JsonVal::Num) return (float)v->num;
  if (v->kind == JsonVal::Str) return parseFloatString(v->str, def);
  return def;  // null / bool / obj / arr → default (fork-datapoint-lenient-leaf: the .cs GetString()
               // on a non-string throws and aborts the WHOLE import; sw degrades per-field. Named.)
}

}  // namespace sw
