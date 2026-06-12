// runtime/bypass_selftest_shared — the stub-op kit shared by the TWO bypass selftest TUs:
//   --selftest-bypasscook     (bypass_cook_selftest.cpp: atomic-flow legs P/C/T + cycle leg R)
//   --selftest-bypasscompound (app/bypass_compound_selftest.cpp: 修C compound legs, 批次9)
// Split out along this seam when the compound legs would have pushed bypass_cook_selftest.cpp
// past the 400-line law. Selftest-only: capture globals + cook stubs under real builtin spec
// names + the shared atomic symbol shapes + the dynamic-spec install for the two test-only op
// shapes (CmdJam Command->Command, TexFilter Texture2D->Texture2D).
#pragma once
#include <vector>

#include "runtime/compound_graph.h"  // Symbol / SlotDef
#include "runtime/render_command.h"  // RenderCommand (+ fwd-decl of MTL::Buffer)
#include "runtime/tixl_point.h"      // SwPoint

namespace MTL {
class Texture;
}  // namespace MTL

namespace sw::bypass_st {

// --- capture globals (one selftest process; reset per leg) ---
extern std::vector<SwPoint>* g_bag;       // the bag DrawPoints saw
extern const MTL::Buffer* g_drawSeenBuf;  // the buffer DrawPoints' item borrows
extern RenderCommand g_chain;             // the chain the RenderTarget executor got
extern MTL::Texture* g_rtTex;             // the texture RenderTarget cooked into
extern MTL::Texture* g_filterTex;         // the texture TexFilter cooked into
extern int g_rtRuns, g_filterRuns, g_jamRuns, g_modRuns;

// Register the stub cooks under the shared spec names (RadialPoints gen = per-point DISTINCT
// positions / ParticleSystem mod = x*2 / DrawPoints capture+1-item chain / CmdJam garbage item /
// RenderTarget chain+tex capture / TexFilter tex capture) AND inject the NodeSpecs for the two
// shapes with no builtin (specFromSymbol + setDynamicSpecs, blessed for headless selftests).
void installStubs();
// Drop the injected test specs (leave the dynamic table as the process found it).
void removeStubSpecs();

// An atomic symbol shape (= the whitelist gate's input: childIsBypassable reads THESE).
Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs);
// The six shared shapes, by spec name.
Symbol symGen();     // RadialPoints: Count Float(6) -> points Points
Symbol symMod();     // ParticleSystem: emit Points + forces ParticleForce -> result Points
Symbol symDraw();    // DrawPoints: points Points -> out Command
Symbol symJam();     // CmdJam: command Command -> out Command (test-only)
Symbol symRT();      // RenderTarget: command Command -> out Texture2D
Symbol symFilter();  // TexFilter: tex Texture2D -> out Texture2D (test-only)

// The gen stub's bag, for point-for-point compares: index i = {1+10i, 0.5i, i}.
inline float genX(int i) { return 1.0f + 10.0f * (float)i; }
inline float genY(int i) { return 0.5f * (float)i; }
inline float genZ(int i) { return (float)i; }

}  // namespace sw::bypass_st
