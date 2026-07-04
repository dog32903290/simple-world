// runtime/datetime_host — shared DateTime kernel implementation (see header for the family doc).
// runtime leaf: pure computation, no hardware, no UI.
#include "runtime/datetime_host.h"

#include <cctype>
#include <chrono>
#include <cstdlib>

namespace sw {

namespace {
// The goldens' determinism seam: <0 = production (real wall clock), >=0 = pinned epoch.
double g_nowOverride = -1.0;
}  // namespace

void setHostNowOverrideForTest(double epochSecs) { g_nowOverride = epochSecs; }

double hostNowEpochSeconds() {
  if (g_nowOverride >= 0.0) return g_nowOverride;
  // system_clock's epoch is Unix epoch (C++20 guarantee; true on every libc++ before that too).
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}

// days-from-civil (Howard Hinnant, http://howardhinnant.github.io/date_algorithms.html) — the
// standard proleptic-Gregorian day count. Matches .NET DateTime's calendar for all years [1..9999].
long long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);                       // [0, 399]
  const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u +
                       static_cast<unsigned>(d) - 1u;                              // [0, 365]
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;                   // [0, 146096]
  return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

namespace {

// Read 1-4 decimal digits (at least minD, at most maxD) at s[i]; advance i; false if fewer than minD.
bool readInt(const std::string& s, size_t& i, int minD, int maxD, int& out) {
  int digits = 0;
  long v = 0;
  while (i < s.size() && digits < maxD && std::isdigit(static_cast<unsigned char>(s[i]))) {
    v = v * 10 + (s[i] - '0');
    ++i;
    ++digits;
  }
  if (digits < minD) return false;
  out = static_cast<int>(v);
  return true;
}

}  // namespace

// "yyyy-M-d[ H:m[:s[.fff]]]" (' ' or 'T' separator, optional trailing 'Z'), leading/trailing ASCII
// whitespace tolerated (C# TryParse trims). See fork-datetime-tryparse-narrow-vocabulary (header).
bool tryParseDateTimeToEpoch(const std::string& s, double& epochOut) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  if (b == e) return false;

  size_t i = b;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  double sec = 0.0;

  if (!readInt(s, i, 4, 4, y)) return false;
  if (i >= e || s[i] != '-') return false;
  ++i;
  if (!readInt(s, i, 1, 2, mo)) return false;
  if (i >= e || s[i] != '-') return false;
  ++i;
  if (!readInt(s, i, 1, 2, d)) return false;

  if (i < e) {  // optional time part
    if (s[i] != ' ' && s[i] != 'T') return false;
    ++i;
    if (!readInt(s, i, 1, 2, h)) return false;
    if (i >= e || s[i] != ':') return false;
    ++i;
    if (!readInt(s, i, 1, 2, mi)) return false;
    if (i < e && s[i] == ':') {  // optional seconds
      ++i;
      int si = 0;
      if (!readInt(s, i, 1, 2, si)) return false;
      sec = si;
      if (i < e && s[i] == '.') {  // optional fraction
        ++i;
        double scale = 0.1;
        bool any = false;
        while (i < e && std::isdigit(static_cast<unsigned char>(s[i]))) {
          sec += (s[i] - '0') * scale;
          scale *= 0.1;
          ++i;
          any = true;
        }
        if (!any) return false;
      }
    }
    if (i < e && s[i] == 'Z') ++i;  // ISO UTC suffix tolerated (route B is UTC anyway)
  }
  if (i != e) return false;  // whole token must be consumed (C# rejects trailing junk)

  // Field range checks (C# TryParse rejects month 13 / day 32 / hour 25 / minute 61).
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec >= 61.0) return false;

  epochOut = static_cast<double>(daysFromCivil(y, mo, d)) * 86400.0 + h * 3600.0 + mi * 60.0 + sec;
  return true;
}

}  // namespace sw
