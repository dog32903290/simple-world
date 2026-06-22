// runtime/string_ops_buildrandomstring_wrap — InsertLineWraps body (verbatim port of
// BuildRandomString.cs:176-300). Split from the core leaf to keep both under the 400-line cap.
//
// All four branches mutate `sb` (the StringBuilder twin) IN PLACE, exactly as the .cs does. Index
// arithmetic is 1:1 with the .cs; std::string::operator[] is the StringBuilder[] indexer, .erase the
// .Remove, .insert the .Insert. No allocation differences observable downstream (the buffer's final
// char sequence is what BuildRandomString.Result emits).
#include "runtime/string_ops_buildrandomstring_wrap.h"

namespace sw {

void insertLineWraps(WrapLinesModes lineWrap, std::string& sb, int insertPos, int insertLength,
                     int wrapColumn) {
  const int len = static_cast<int>(sb.size());

  if (lineWrap == WrapLinesModes::WrapAtCharacters) {
    // .cs:178-203 — look back to the previous '\n', measure the line, break if it overflows wrapColumn.
    int lookBackIndex = insertPos;
    while (lookBackIndex > 0 && sb[lookBackIndex] != '\n') {
      lookBackIndex--;
    }

    int lineLength = insertPos - lookBackIndex + insertLength;
    if (lineLength > wrapColumn && insertPos > 0 && insertPos < static_cast<int>(sb.size())) {
      sb[insertPos - 1] = '\n';
      return;
    }

    int lookForwardIndex = insertPos;
    while (lookForwardIndex < static_cast<int>(sb.size()) && sb[lookForwardIndex] != '\n') {
      lookForwardIndex++;
    }

    if (lookForwardIndex - lookBackIndex > wrapColumn) {
      // .cs indexes sb[insertPos + insertLength - 1] (guarded by the caller's pos+insertLength range).
      const int idx = insertPos + insertLength - 1;
      if (idx >= 0 && idx < static_cast<int>(sb.size())) sb[idx] = '\n';
    }
  } else if (lineWrap == WrapLinesModes::WrapAtWords) {
    // .cs:204-237 — walk the whole buffer, break at the last word-boundary when a line overflows.
    int pos = 0;
    int currentLineLength = 0;
    int lastValidBreakPos = -1;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];
      if (c == '\n') {
        currentLineLength = 0;
        lastValidBreakPos = -1;
      } else if (c == ' ' || c == '.' || c == ',' || c == '/') {
        lastValidBreakPos = pos;
        currentLineLength++;
      } else {
        currentLineLength++;
      }

      if (currentLineLength > wrapColumn && lastValidBreakPos != -1) {
        sb[lastValidBreakPos] = '\n';
        pos = lastValidBreakPos;
        lastValidBreakPos = -1;
        currentLineLength = 0;
      }
      pos++;
    }
  } else if (lineWrap == WrapLinesModes::WrapToFillBlock) {
    // .cs:238-274 — like WrapAtWords, but EXISTING '\n' become spaces first (re-flow into a fill block).
    int pos = 0;
    int currentLineLength = 0;
    int lastValidBreakPos = -1;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];
      if (c == '\n') {
        sb[pos] = ' ';
        lastValidBreakPos = pos;
        currentLineLength++;
      } else if (c == ' ' || c == '.' || c == ',' || c == '/') {
        lastValidBreakPos = pos;
        currentLineLength++;
      } else {
        currentLineLength++;
      }

      if (currentLineLength > wrapColumn && lastValidBreakPos != -1) {
        sb[lastValidBreakPos] = '\n';
        pos = lastValidBreakPos;
        lastValidBreakPos = -1;
        currentLineLength = 0;
      }
      pos++;
    }
  } else if (lineWrap == WrapLinesModes::SolidBlock) {
    // .cs:275-299 — strip every '\n', then hard-break at exactly wrapColumn columns (a solid grid).
    int pos = 0;
    int currentLineLength = 0;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];

      if (c == '\n') {
        sb.erase(static_cast<std::string::size_type>(pos), 1);
        continue;  // .cs: continue WITHOUT advancing pos (re-examine the shifted char)
      }

      currentLineLength++;
      pos++;

      if (currentLineLength == wrapColumn) {
        sb.insert(static_cast<std::string::size_type>(pos), 1, '\n');
        pos++;
        currentLineLength = 0;
      }
    }
  }
  // WrapLinesModes::DontWrap → no-op (the .cs switch has no DontWrap branch).
  (void)len;
}

}  // namespace sw
