// BlendVector3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/BlendVector3.cs (+ BlendVector3.t3 defaults).
//
//   BlendVector3.cs Update():
//     Result.Value = Vector3.Zero;
//     var collectedTypedInputs = Vectors.GetCollectedTypedInputs();
//     var count = collectedTypedInputs.Count;
//     if (count == 0) return;
//     var f = F.GetValue(context);
//     var index1 = (int)MathUtils.Fmod((int)f, count);
//     var index2 = (int)MathUtils.Fmod((int)(f+1), count);
//     var mix = MathUtils.Fmod(f, 1);
//     Result.Value = MathUtils.Lerp(collectedTypedInputs[index1].GetValue(context),
//                                   collectedTypedInputs[index2].GetValue(context),
//                                   mix);
//
//   Ports: Vectors = MultiInputSlot<Vector3> (variable N Vec3 wires); F = InputSlot<float> (default 0.0).
//   Output: Result (Slot<Vector3>) — 3 Float ports (Result.x, Result.y, Result.z).
//   BlendVector3.t3 defaults: Vectors = {X:0, Y:0, Z:0}; F = 0.0.
//
// EVAL-SIDE LAYOUT (resident path — multiInput):
//   Resident gather expands Vectors into in[] PREFIX, each Vec3 = 3 consecutive floats (x, y, z).
//   Trailing regular port F = in[n-1]. n = 3K+1, K = number of connected Vec3 sources.
//   index1 = Fmod((int)F, K);  index2 = Fmod((int)(F+1.0f), K);  mix = Fmod(F, 1.0f).
//   Result component (0=x, 1=y, 2=z) = Lerp(src[index1][comp], src[index2][comp], mix).
//   Output ports Result.x/.y/.z at spec indices 2/3/4; comp = outIdx - 2.
//
// FORKS (named):
//   - fork-blendvec3-vec3-as-3-floats (fork-vec4-decompose-arity precedent): each Vec3 wire in
//     the multiInput produces 3 consecutive Float values in in[]. Same convention as
//     PickVector3 (value_op_pickvector3.cpp). Output is also 3 Float ports.
//   - fork-blendvec3-fmod-floor: TiXL MathUtils.Fmod is C# Math.IEEERemainder / custom floor-mod.
//     Standard C fmodf(a,b) for b>0 and a>=0 is identical; for a<0 it returns a negative remainder.
//     Faithful: use fmodf then adjust negative results (same as MathUtils.Fmod semantics).
//   - fork-blendvec3-empty-passthrough: count==0 → Result = Vector3.Zero. Runtime returns 0.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runBlendVector3SelfTest(bool injectBug);

namespace {

// TiXL MathUtils.Fmod: floor-mod that always returns non-negative for positive divisor.
// For b==0 TiXL is undefined; return 0.
static float tixlFmod(float a, float b) {
  if (b == 0.0f) return 0.0f;
  float r = std::fmod(a, b);
  if (r < 0.0f) r += b;  // fork-blendvec3-fmod-floor
  return r;
}

// in[] layout for ONE Vec3 multiInput (K sources, each 3 floats) + trailing scalar F:
//   [src0.x, src0.y, src0.z, ..., srcK-1.x, .y, .z, F]   n = 3K+1.
// K = (n-1)/3. Component comp = outIdx-2 ∈ {0=x,1=y,2=z}.
// index1 = Fmod((int)F, K); index2 = Fmod((int)(F+1), K); mix = Fmod(F, 1).
// Result[comp] = Lerp(src[index1][comp], src[index2][comp], mix).
float evalBlendVector3(int outIdx, const float* in, int n, const EvaluationContext&) {
  const int comp = outIdx - 2;  // 0=x, 1=y, 2=z
  if (comp < 0 || comp > 2) return 0.0f;
  if (n < 4) return 0.0f;  // need at least 1 Vec3 (3 floats) + F
  const int K = (n - 1) / 3;
  if (K == 0) return 0.0f;  // fork-blendvec3-empty-passthrough

  const float fv = in[n - 1];
  const int i1 = (int)tixlFmod((float)(int)fv,        (float)K);
  const int i2 = (int)tixlFmod((float)(int)(fv + 1),   (float)K);
  const float mix = tixlFmod(fv, 1.0f);

  const float a = in[3 * i1 + comp];
  const float b = in[3 * i2 + comp];
  return a + (b - a) * mix;  // MathUtils.Lerp (standard linear)
}

}  // namespace

static const ValueOp _reg_blendvector3{
    // BlendVector3 (TiXL Lib.numbers.vec3.BlendVector3):
    //   Result = Lerp(Vectors[Fmod(int(F), K)], Vectors[Fmod(int(F)+1, K)], Fmod(F,1)).
    // Port order: Vectors (multiInput Vec3 head, vecArity=3), F (trailing scalar), Result.x/.y/.z.
    // Defaults from BlendVector3.t3: Vectors = {X:0,Y:0,Z:0}; F = 0.0.
    // fork-blendvec3-vec3-as-3-floats: Vec3 multiInput = 3 Float ports per source in in[].
    {"BlendVector3", "BlendVector3",
     {{"Vectors",   "Vectors",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3,
       /*multiInput=*/true},
      {"F",         "F",         "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider},
      {"Result.x",  "Result.x",  "Float", false},
      {"Result.y",  "Result.y",  "Float", false},
      {"Result.z",  "Result.z",  "Float", false}},
     evalBlendVector3},
    "blendvector3", runBlendVector3SelfTest};

// --- BlendVector3 MATH golden (resident path — multiInput) ------------------------------------
// 3 Vec3 sources: A=(1,2,3), B=(4,5,6), C=(7,8,9).
// Resident gather: 9 Const nodes (A.x/A.y/A.z/.../C.z) wired into Vectors multiInput + 1 Const F.
// Test cases (K=3):
//   F=0.0 → index1=Fmod(0,3)=0=A; index2=Fmod(1,3)=1=B; mix=0 → Result = A = (1,2,3).
//   F=1.0 → index1=1=B; index2=2=C; mix=0 → Result = B = (4,5,6).
//   F=1.5 → index1=1=B; index2=2=C; mix=0.5 → Result = Lerp(B,C,0.5) = (5.5,6.5,7.5).
//   F=2.7 → index1=Fmod(2,3)=2=C; index2=Fmod(int(3.7)=3,3)=0=A; mix=0.7 → (2.8,3.8,4.8).
//   F=-0.5 → int(-0.5)=0; index1=Fmod(0,3)=0=A; int(-0.5+1.0)=int(0.5)=0;
//            index2=Fmod(0,3)=0=A; mix=Fmod(-0.5,1)=0.5; Result=Lerp(A,A,0.5)=A=(1,2,3).
//            [TiXL: (int)(f+1) casts the sum → degenerate blend: same source both sides]

namespace {
Symbol bv3Atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

int runBlendVector3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Symbols: Const (scalar) + BlendVector3.
  Symbol cst = bv3Atomic("Const",
                          {{"value", "value", "Float", 0.0f}},
                          {{"out",   "out",   "Float", 0.0f}});
  Symbol bv3 = bv3Atomic("BlendVector3",
                          {{"Vectors", "Vectors", "Float", 0.0f},
                           {"F",       "F",       "Float", 0.0f}},
                          {{"Result.x", "Result.x", "Float", 0.0f},
                           {"Result.y", "Result.y", "Float", 0.0f},
                           {"Result.z", "Result.z", "Float", 0.0f}});

  // Build a BlendVector3 graph with 3 Vec3 sources and a given F, return one component.
  // Sources: A=(1,2,3), B=(4,5,6), C=(7,8,9). Each Vec3 = 3 Const nodes wired to Vectors.
  // Const IDs: ax=1,ay=2,az=3, bx=4,by=5,bz=6, cx=7,cy=8,cz=9, F=10, BlendVec3=11.
  auto blendWith = [&](float fval, int comp) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[bv3.id] = bv3;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"Result.x","Result.x","Float",0.0f},
                       {"Result.y","Result.y","Float",0.0f},
                       {"Result.z","Result.z","Float",0.0f}};

    // 9 component consts + 1 F const + 1 BlendVector3 node.
    // A=(1,2,3), B=(4,5,6), C=(7,8,9).
    SymbolChild ca_x; ca_x.id=1; ca_x.symbolId="Const"; ca_x.overrides["value"]=1.0f;
    SymbolChild ca_y; ca_y.id=2; ca_y.symbolId="Const"; ca_y.overrides["value"]=2.0f;
    SymbolChild ca_z; ca_z.id=3; ca_z.symbolId="Const"; ca_z.overrides["value"]=3.0f;
    SymbolChild cb_x; cb_x.id=4; cb_x.symbolId="Const"; cb_x.overrides["value"]=4.0f;
    SymbolChild cb_y; cb_y.id=5; cb_y.symbolId="Const"; cb_y.overrides["value"]=5.0f;
    SymbolChild cb_z; cb_z.id=6; cb_z.symbolId="Const"; cb_z.overrides["value"]=6.0f;
    SymbolChild cc_x; cc_x.id=7; cc_x.symbolId="Const"; cc_x.overrides["value"]=7.0f;
    SymbolChild cc_y; cc_y.id=8; cc_y.symbolId="Const"; cc_y.overrides["value"]=8.0f;
    SymbolChild cc_z; cc_z.id=9; cc_z.symbolId="Const"; cc_z.overrides["value"]=9.0f;
    SymbolChild cf;   cf.id  =10; cf.symbolId  ="Const"; cf.overrides["value"]  =fval;
    SymbolChild bv;   bv.id  =11; bv.symbolId  ="BlendVector3";
    root.children = {ca_x,ca_y,ca_z, cb_x,cb_y,cb_z, cc_x,cc_y,cc_z, cf, bv};

    const char* outName = (comp==0)?"Result.x":(comp==1)?"Result.y":"Result.z";
    root.connections = {
        {1,"out",11,"Vectors"},  // A.x
        {2,"out",11,"Vectors"},  // A.y
        {3,"out",11,"Vectors"},  // A.z
        {4,"out",11,"Vectors"},  // B.x
        {5,"out",11,"Vectors"},  // B.y
        {6,"out",11,"Vectors"},  // B.z
        {7,"out",11,"Vectors"},  // C.x
        {8,"out",11,"Vectors"},  // C.y
        {9,"out",11,"Vectors"},  // C.z
        {10,"out",11,"F"},
        {11, outName, kSymbolBoundary, outName},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find(outName);
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  // CASE 1: F=0.0 → i1=0(A), i2=1(B), mix=0 → Result=A=(1,2,3).
  // injectBug asserts wrong x=4 (source B.x) — wrong index selection.
  {
    float rx = blendWith(0.0f, 0);
    float want = injectBug ? 4.0f : 1.0f;
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=0.0 Result.x=%.4f want=%.4f -> %s\n",
           rx, want, pass?"PASS":"FAIL");
  }
  {
    float ry = blendWith(0.0f, 1);
    bool pass = std::fabs(ry - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=0.0 Result.y=%.4f want=2.0000 -> %s\n", ry, pass?"PASS":"FAIL");
  }
  {
    float rz = blendWith(0.0f, 2);
    bool pass = std::fabs(rz - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=0.0 Result.z=%.4f want=3.0000 -> %s\n", rz, pass?"PASS":"FAIL");
  }

  // CASE 2: F=1.5 → int(1.5)=1=B, int(2.5)=2=C, mix=0.5 → Lerp(B,C,0.5) = (5.5,6.5,7.5).
  {
    float rx = blendWith(1.5f, 0);
    bool pass = std::fabs(rx - 5.5f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=1.5 Result.x=%.4f want=5.5000 (Lerp B->C,0.5) -> %s\n",
           rx, pass?"PASS":"FAIL");
  }
  {
    float rz = blendWith(1.5f, 2);
    bool pass = std::fabs(rz - 7.5f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=1.5 Result.z=%.4f want=7.5000 (Lerp B->C,0.5) -> %s\n",
           rz, pass?"PASS":"FAIL");
  }

  // CASE 3: F=2.7 → int(2.7)=2=C; int(2.7+1.0)=int(3.7)=3→Fmod(3,3)=0=A; mix=0.7.
  //   Lerp(C,A,0.7): x=7+(1-7)*0.7=2.8; y=8+(2-8)*0.7=3.8; z=9+(3-9)*0.7=4.8.
  {
    float rx = blendWith(2.7f, 0);
    float want_x = 7.0f + (1.0f - 7.0f)*0.7f;  // 2.8
    bool pass = std::fabs(rx - want_x) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=2.7 Result.x=%.4f want=%.4f (Lerp C->A,0.7) -> %s\n",
           rx, want_x, pass?"PASS":"FAIL");
  }
  {
    float ry = blendWith(2.7f, 1);
    float want_y = 8.0f + (2.0f - 8.0f)*0.7f;  // 3.8
    bool pass = std::fabs(ry - want_y) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=2.7 Result.y=%.4f want=%.4f (Lerp C->A,0.7) -> %s\n",
           ry, want_y, pass?"PASS":"FAIL");
  }
  {
    float rz = blendWith(2.7f, 2);
    float want_z = 9.0f + (3.0f - 9.0f)*0.7f;  // 4.8
    bool pass = std::fabs(rz - want_z) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=2.7 Result.z=%.4f want=%.4f (Lerp C->A,0.7) -> %s\n",
           rz, want_z, pass?"PASS":"FAIL");
  }

  // CASE 6: F=-0.5 (negative F bug coverage — TiXL (int)(f+1) path).
  //   int(-0.5)=0; index1=Fmod(0,3)=0=A.
  //   int(-0.5+1.0)=int(0.5)=0; index2=Fmod(0,3)=0=A.
  //   mix=Fmod(-0.5,1.0)=0.5; Lerp(A,A,0.5)=A=(1,2,3). [degenerate: same source both sides]
  //   injectBug would use (int)fv+1=1 → index2=B → Lerp(A,B,0.5)=(2.5,3.5,4.5) — wrong.
  {
    float rx = blendWith(-0.5f, 0);
    float want_x = 1.0f;  // A.x
    bool pass = std::fabs(rx - want_x) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=-0.5 Result.x=%.4f want=%.4f (degenerate A==A) -> %s\n",
           rx, want_x, pass?"PASS":"FAIL");
  }
  {
    float ry = blendWith(-0.5f, 1);
    float want_y = 2.0f;  // A.y
    bool pass = std::fabs(ry - want_y) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=-0.5 Result.y=%.4f want=%.4f (degenerate A==A) -> %s\n",
           ry, want_y, pass?"PASS":"FAIL");
  }
  {
    float rz = blendWith(-0.5f, 2);
    float want_z = 3.0f;  // A.z
    bool pass = std::fabs(rz - want_z) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=-0.5 Result.z=%.4f want=%.4f (degenerate A==A) -> %s\n",
           rz, want_z, pass?"PASS":"FAIL");
  }

  // CASE 4: F=1.0 → int(1)=1=B, int(2)=2=C, mix=0 → Result=B=(4,5,6) (exact boundary).
  {
    float rx = blendWith(1.0f, 0);
    bool pass = std::fabs(rx - 4.0f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=1.0 Result.x=%.4f want=4.0000 (B,no-mix) -> %s\n",
           rx, pass?"PASS":"FAIL");
  }

  // CASE 5: F=3.0 → int(3)→Fmod(3,3)=0=A; int(4)→Fmod(4,3)=1=B; mix=0 → Result=A=(1,2,3) (wrap).
  {
    float rx = blendWith(3.0f, 0);
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-blendvector3] F=3.0 Result.x=%.4f want=1.0000 (wrap to A) -> %s\n",
           rx, pass?"PASS":"FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
