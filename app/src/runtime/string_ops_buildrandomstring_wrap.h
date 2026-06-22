// runtime/string_ops_buildrandomstring_wrap — the InsertLineWraps line-wrap helper for BuildRandomString.
//
// Split out of string_ops_buildrandomstring.cpp so BOTH leaves stay under the 400-line cap (ARCHITECTURE.md
// rule 4 — the core cook + this 4-mode wrap routine together exceed 400). This is the verbatim port of
// BuildRandomString.cs:InsertLineWraps (the WrapLinesModes switch) operating on a std::string in place
// (TiXL mutates the StringBuilder in place; std::string is the sw StringBuilder twin per the seam's
// StringBuilder=String channel decision).
//
// WrapLinesModes order MUST match the .cs enum (the WrapLines Int param is the enum index):
//   0 DontWrap, 1 WrapAtWords, 2 WrapAtCharacters, 3 WrapToFillBlock, 4 SolidBlock.
// (BuildRandomString.t3 default WrapLines = 1 = WrapAtWords.)
#pragma once

#include <string>

namespace sw {

enum class WrapLinesModes {
  DontWrap = 0,
  WrapAtWords = 1,
  WrapAtCharacters = 2,
  WrapToFillBlock = 3,
  SolidBlock = 4,
};

// Verbatim port of BuildRandomString.cs:InsertLineWraps(lineWrap, stringBuilder, insertPos, insertLength,
// wrapColumn). Mutates `sb` in place. DontWrap → no-op (the .cs switch has no DontWrap case). insertPos /
// insertLength are only read by WrapAtCharacters (the look-back/look-forward window around the insertion);
// the word/fill/solid modes scan the whole buffer and ignore them (1:1 with the .cs).
void insertLineWraps(WrapLinesModes lineWrap, std::string& sb, int insertPos, int insertLength,
                     int wrapColumn);

}  // namespace sw
