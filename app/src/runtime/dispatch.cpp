#include "runtime/dispatch.h"

#include <cstdio>

namespace sw {
int runDispatchSelfTest() {
  struct Case {
    uint32_t total, group, want;
  };
  // Golden from COMMAND_STREAM_CONTRACT (+ a couple of derived checks).
  const Case cases[] = {
      {64, 16, 5}, {63, 16, 4}, {64, 0, 0}, {960, 16, 61}, {540, 16, 34}, {1, 16, 1},
  };
  bool pass = true;
  for (const auto& c : cases) {
    uint32_t got = calcDispatchCount(c.total, c.group);
    bool ok = got == c.want;
    printf("[selftest-dispatch] calc(%u,%u)=%u want=%u %s\n", c.total, c.group, got, c.want,
           ok ? "ok" : "BAD");
    if (!ok) pass = false;
  }
  printf("[selftest-dispatch] -> %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}
}  // namespace sw
