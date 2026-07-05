// runtime/point_ops_forwardbeattaps — ForwardBeatTaps: publish beat-tap / resync / slide-sync into the
// process-global TapProvider, then forward the cooked Command subtree. TiXL numbers/anim/vj/ForwardBeatTaps.cs.
//
// The provider WRITE happens in the cook driver (both legs) BEFORE the SubTree cook — the SAME shape as
// cmdVarPush (S3a) and the SetRequestedResolution push: the driver owns the ordering because it owns the
// subtree recursion. ForwardBeatTaps.cs:22-38 does triggers-then-SubTree.GetValue, so the write is a
// pre-subtree side effect; forwardBeatTapsApply() is that side effect, called from the driver's Command
// branch (like logMessageCurrentText). The op cook itself only FORWARDS the cooked subtree's items.
//
// Its own tiny header (NOT the at-cap point_ops.h) so the two cook drivers can call the pre-subtree write
// without pulling the god-header. Bodies in point_ops_forwardbeattaps.cpp.
#pragma once
#include <map>
#include <string>

namespace sw {

// True iff opType is ForwardBeatTaps. The driver's Command branch consults this to decide whether to write
// the TapProvider (edge-detect triggers + slide-sync) BEFORE cooking the node's SubTree.
bool isForwardBeatTaps(const std::string& opType);

// PRE-SUBTREE WRITE (ForwardBeatTaps.cs:22-35): edge-detect TriggerBeatTap / TriggerResync (>0.5 = the
// bool level) against the TapProvider's stored previous state and publish the rising-edge pulses; set
// SlideSyncTime from SlideSyncTimeOffset unless it is NaN. `params` = the node's RESOLVED Float params.
// Call BEFORE cooking the SubTree. No-op when the -bug flag skips it (the golden's severed-write tooth).
void forwardBeatTapsApply(const std::map<std::string, float>& params);

// Command-op registrar (Command/SubTree in → Command out, forwards the cooked subtree items). The provider
// write is in the driver; this op only forwards (like SetRequestedResolution).
void registerForwardBeatTapsOp();

// -bug DRIVER flag (mirror of setVarBugSkipWrite): when true, the driver SKIPS forwardBeatTapsApply on BOTH
// legs → the provider never edges → the golden's probe reads no pulse → RED. OFF in production.
bool& forwardBeatTapsBugSkipWrite();

// --selftest-forwardbeattaps (the HARD-GATE golden; the driver-side pre-subtree write, both legs).
int runForwardBeatTapsSelfTest(bool injectBug);

}  // namespace sw
