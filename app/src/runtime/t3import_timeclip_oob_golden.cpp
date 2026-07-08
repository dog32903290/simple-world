// runtime/t3import_timeclip_oob_golden — regression tooth for the parseChildTimeClips heap-OOB
// (--selftest-t3-timeclip-oob). CRASH-CLASS bug, not a parity drift.
//
// Repro (ready-set scan, 2026-07-08): `simple_world --probe-import DrawMesh.t3` (and
// FindClosestPointsOnMesh.t3) aborted rc=134 — ASan heap-buffer-overflow READ of size 4, "8 bytes
// before a 32-byte region", inside sw::parseChildTimeClips. Root cause: crude_json's CONST
// operator[](key) does NOT return a null for an absent key — it dereferences map::end()->second
// (its CRUDE_ASSERT is compiled out under NDEBUG = the release/ASan build), so the FIRST touch of the
// returned dangling ref (`.is_object()` reads the 4-byte m_Type) reads 8 bytes before a freed node.
// Both .t3 carry a non-empty Outputs entry that is a DirtyFlagTrigger-only slot with NO "OutputData"
// (DrawMesh: {"Id":"7a76d147…","DirtyFlagTrigger":"Always"}); the old `ov["OutputData"]` read OOB.
//
// Fix (t3_import_clips.cpp): route every OPTIONAL-key read through optField()/optStr(), which
// `.contains()`-guards before operator[] and returns a static null value for an absent key.
//
// This tooth feeds parseChildTimeClips three hand-built crude_json children directly (unit-level, no
// binary probe needed) and asserts BOTH halves of a real fix, not a mask:
//   ① the DirtyFlagTrigger-only shape (absent OutputData) → NO crash, clips stays EMPTY;
//   ② a well-formed TimeClip → the parsed timeStart/End, sourceStart/End, layerIndex are the AUTHORED
//      numbers (a mask that swallowed data would fail this);
//   ③ a TimeClip with SourceRange + LayerIndex ABSENT → SourceRange conforms to TimeRange, layerIndex
//      defaults to 0 (these absent optional keys ALSO went through the const operator[] → the guard
//      must cover them too, not just OutputData).
// injectBug flips t3ClipOobGuardDisable() → optField reverts to the bare, unguarded const operator[];
// re-running fixture ① then reads 8 bytes OOB → ASan aborts the -bug process (rc≠0) = the tooth BITES.
//
// ZONE: runtime golden (shell tier — binds the importer's clip parse to crafted JSON fixtures).
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "crude_json.h"
#include "runtime/compound_graph.h"       // SymbolChild / ClipTimeData
#include "runtime/t3_import_internal.h"   // parseChildTimeClips (sw::)

namespace sw {

// Defined in t3_import_clips.cpp (the file under test). -bug leg turns the boundary guard OFF.
bool& t3ClipOobGuardDisable();

namespace {

constexpr float kEps = 1e-4f;
bool near(float a, float b) { return std::fabs(a - b) < kEps; }

// ① DirtyFlagTrigger-only Outputs entry — the exact DrawMesh.t3 shape (Id present, OutputData ABSENT).
const char* const kDirtyFlagOnly =
    "{ \"Outputs\": [ { \"Id\": \"7a76d147-4b8e-48cf-aa3e-aac3aa90e888\","
    " \"DirtyFlagTrigger\": \"Always\" } ] }";

// ② Well-formed TimeClip on FloatsToBuffer's real "Buffer" output guid (t3_import_maps kTable) so the
// slot resolves and the clip is STORED — lets us read the parsed values back.
const char* const kFullClip =
    "{ \"Outputs\": [ { \"Id\": \"f5531ffb-dbde-45d3-af2a-bd90bcbf3710\","
    " \"OutputData\": { \"Type\": \"T3.Core.Animation.TimeClip\","
    " \"TimeClip\": { \"TimeRange\": { \"Start\": 2.5, \"End\": 7.0 },"
    " \"SourceRange\": { \"Start\": 1.0, \"End\": 3.0 }, \"LayerIndex\": 4 } } } ] }";

// ③ TimeClip with SourceRange + LayerIndex ABSENT (both optional keys the guard must also cover).
const char* const kConformClip =
    "{ \"Outputs\": [ { \"Id\": \"f5531ffb-dbde-45d3-af2a-bd90bcbf3710\","
    " \"OutputData\": { \"Type\": \"T3.Core.Animation.TimeClip\","
    " \"TimeClip\": { \"TimeRange\": { \"Start\": 2.0, \"End\": 5.0 } } } } ] }";

}  // namespace

int runT3TimeClipOobRegression(bool injectBug) {
  const auto warn = [](const std::string&) {};  // swallow: fixtures use mapped slots, no warns expected
  t3ClipOobGuardDisable() = injectBug;

  int fails = 0;

  // ── ① DirtyFlagTrigger-only: MUST NOT crash; clips stays empty. Under -bug the bare operator[]
  //    reads 8 bytes OOB here → ASan aborts (rc≠0) = BITE. (No return reached under ASan on -bug.)
  {
    crude_json::value cv = crude_json::value::parse(kDirtyFlagOnly);
    SymbolChild child;
    parseChildTimeClips(cv, "DrawMesh", child, warn);
    if (!child.clips.empty()) {
      printf("[t3-timeclip-oob] FAIL ①: DirtyFlagTrigger-only produced %zu clip(s), want 0\n",
             child.clips.size());
      ++fails;
    } else {
      printf("[t3-timeclip-oob] ① DirtyFlagTrigger-only: no crash, clips empty (OK)\n");
    }
  }

  // ── ② Full TimeClip → authored values parsed exactly (real fix, not a swallow).
  {
    crude_json::value cv = crude_json::value::parse(kFullClip);
    SymbolChild child;
    parseChildTimeClips(cv, "FloatsToBuffer", child, warn);
    auto it = child.clips.find("Buffer");
    if (it == child.clips.end()) {
      printf("[t3-timeclip-oob] FAIL ②: no clip on 'Buffer' slot\n");
      ++fails;
    } else {
      const ClipTimeData& c = it->second;
      const bool ok = near(c.timeStart, 2.5f) && near(c.timeEnd, 7.0f) &&
                      near(c.sourceStart, 1.0f) && near(c.sourceEnd, 3.0f) && c.layerIndex == 4;
      printf("[t3-timeclip-oob] ② Buffer clip time[%.3f,%.3f] src[%.3f,%.3f] layer=%d (want "
             "[2.500,7.000] [1.000,3.000] 4) %s\n",
             c.timeStart, c.timeEnd, c.sourceStart, c.sourceEnd, c.layerIndex, ok ? "OK" : "MISMATCH");
      if (!ok) ++fails;
    }
  }

  // ── ③ Absent SourceRange → conforms to TimeRange; absent LayerIndex → 0.
  {
    crude_json::value cv = crude_json::value::parse(kConformClip);
    SymbolChild child;
    parseChildTimeClips(cv, "FloatsToBuffer", child, warn);
    auto it = child.clips.find("Buffer");
    if (it == child.clips.end()) {
      printf("[t3-timeclip-oob] FAIL ③: no clip on 'Buffer' slot\n");
      ++fails;
    } else {
      const ClipTimeData& c = it->second;
      const bool ok = near(c.timeStart, 2.0f) && near(c.timeEnd, 5.0f) &&
                      near(c.sourceStart, 2.0f) && near(c.sourceEnd, 5.0f) && c.layerIndex == 0;
      printf("[t3-timeclip-oob] ③ conform time[%.3f,%.3f] src[%.3f,%.3f] layer=%d (want src==time, "
             "layer 0) %s\n",
             c.timeStart, c.timeEnd, c.sourceStart, c.sourceEnd, c.layerIndex, ok ? "OK" : "MISMATCH");
      if (!ok) ++fails;
    }
  }

  t3ClipOobGuardDisable() = false;

  if (!injectBug) {
    printf("[t3-timeclip-oob] %s (fails=%d)\n", fails == 0 ? "PASS" : "RED", fails);
    return fails == 0 ? 0 : 1;
  }
  // -bug leg: under ASan the guard-off run above already aborted on fixture ① (rc≠0 = BITE) and we
  // never get here. Reaching here means the OOB read did not fault (non-ASan/unlucky) → the guard was
  // not observably load-bearing on this build: report it as a non-bite (exit 0) so --bite's NO-BITE
  // list surfaces it rather than a false pass.
  printf("[t3-timeclip-oob] -bug: guard-off run did not fault on this build (non-ASan) — NO-BITE\n");
  return 0;
}

}  // namespace sw
