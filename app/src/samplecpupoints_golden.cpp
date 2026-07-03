// samplecpupoints_golden — golden for SampleCpuPoints on the PointList currency (Bezier+quaternion resample):
//   --selftest-samplecpupoints   (SampleCpuPoints: 2-key host list → 1 resampled point at SamplePos)
//
// SampleCpuPoints reads a host PointList, brackets SamplePos between two keys, and emits ONE point: a cubic
// Bezier interpolation of position (control pts driven by TangentScale & key orientations) + a RH look-at
// orientation built from the Bezier tangent and slerped key-up (SampleCpuPoints.cs:20-81). We hand-build a
// 2-point input list and direct-cook the op (the cook driver gathers PointList inputs into inputLists;
// here we synthesize inputLists directly — same posture as the pointstocpu / pointlist goldens).
//
// HAND-COMPUTED GOLDEN (Case A — the .t3-default TangentScale=0 path, SamplePos=0.5):
//   in[0] = { Position=(0,0,0), Orientation=identity (0,0,0,1) }
//   in[1] = { Position=(2,0,0), Orientation=identity (0,0,0,1) }
//   TangentScale=0 → tLength=0 → tA=tB=0 → cubic ctrl pts (posA, posA, posB, posB).
//   f=0.5 → i0=0, i1=1, t=0.5. l=|posB-posA|=2 (> eps → NOT the coincident branch).
//   Bezier(posA,posA,posB,posB, 0.5): pos = 0.5·posA + 0.5·posB = (1,0,0)  (midpoint).
//   tan = Bezier'(...) = 1.5·(posB-posA) = (3,0,0) → forward f = (1,0,0).
//   upA=upB=(0,1,0) (Transform(UnitY, identity)); slerp dot=1 → (0,1,0); up⊥f → (0,1,0).
//   right = normalize(up×f) = normalize((0,0,-1)) = (0,0,-1); up = f×right = (0,1,0).
//   Matrix rows right=(0,0,-1) up=(0,1,0) fwd=(1,0,0): trace=1 → s=√2, w=√2/2;
//     x=(m23-m32)·(0.5/s)=0; y=(m31-m13)·(0.5/s)=(1-(-1))·(0.5/√2)=√2/2; z=(m12-m21)·...=0.
//   EXPECTED: Position=(1,0,0), Orientation=(0, √2/2, 0, √2/2)  (a +90° rotation about Y).
//
//   (Case B — coincident keys: posA==posB → l<=eps → output = key a verbatim. Case C — SamplePos clamp.)
//
// P2 FIX (GOLDEN_ORACLE_AUDIT "samplecpupoints — tangent 未生效"): Case A's TangentScale=0 collapses the
// cubic to Bezier(posA,posA,posB,posB,t) — the tangent/control-point formulas (SampleCpuPoints.cs:52-57)
// never fire. Cases D/E below run TangentScale=1 on NON-coincident keys at mid t=0.25, so the control
// points leave the segment and the cubic diverges from the collapsed curve. Expected values hand-derived
// from SampleCpuPoints.cs + Core/Utils/Splines/Bezier.cs ONLY (python derivations in the case comments;
// formulas transcribed from the .cs — never from the sw implementation).
//
// injectBug routes through pointListInjectBug() so the RED case drops the produced point on the REAL cook
// path (got.size()==0 ≠ 1 → FAIL), teeth on the op not the expected value.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / pointListInjectBug / findPointListOp
#include "runtime/tixl_point.h"             // SwPoint (64B stride) + swPointDefault

namespace sw {

namespace {

const float kEps = 1e-5f;
bool near1(float a, float b) { return std::fabs(a - b) <= kEps; }

// Direct-cook one SampleCpuPoints over a hand-built input list. Returns the resampled host point list.
std::vector<SwPoint> runSample(const std::vector<SwPoint>& in, float samplePos, float tangentScale) {
  std::vector<std::vector<SwPoint>> inputLists;
  inputLists.push_back(in);
  std::map<std::string, float> params;
  params["SamplePos"] = samplePos;
  params["TangentScale"] = tangentScale;
  std::vector<SwPoint> out;

  PointListCookCtx pc;
  pc.inputLists = &inputLists;
  pc.output = &out;
  pc.params = &params;
  const PointListCookFn* fn = findPointListOp("SampleCpuPoints");
  if (fn && *fn) (*fn)(pc);
  return out;
}

SwPoint mkKey(float px, float py, float pz, float qx, float qy, float qz, float qw) {
  SwPoint p = swPointDefault();
  p.Position = {px, py, pz};
  p.Rotation = {qx, qy, qz, qw};
  return p;
}

}  // namespace

int runSampleCpuPointsSelfTest(bool injectBug) {
  bool ok = true;
  const float r2 = std::sqrt(2.0f) * 0.5f;  // √2/2 ≈ 0.70710678

  pointListInjectBug() = injectBug;

  // CASE A — 2 identity keys (0,0,0)->(2,0,0), TangentScale=0, SamplePos=0.5. Position midpoint (1,0,0),
  // Orientation = +90° about Y (0, √2/2, 0, √2/2). EXPECTED is fixed (NOT bug-adjusted): injectBug drops
  // the point on the REAL cook → size 0 ≠ 1 → RED.
  {
    std::vector<SwPoint> in = {mkKey(0, 0, 0, 0, 0, 0, 1), mkKey(2, 0, 0, 0, 0, 0, 1)};
    std::vector<SwPoint> got = runSample(in, 0.5f, 0.0f);
    bool pass = got.size() == 1 && near1(got[0].Position.x, 1.0f) && near1(got[0].Position.y, 0.0f) &&
                near1(got[0].Position.z, 0.0f) && near1(got[0].Rotation.x, 0.0f) &&
                near1(got[0].Rotation.y, r2) && near1(got[0].Rotation.z, 0.0f) &&
                near1(got[0].Rotation.w, r2);
    ok = ok && pass;
    if (got.size() == 1)
      std::printf("[selftest-samplecpupoints] A(mid) pos=(%.4f,%.4f,%.4f) rot=(%.4f,%.4f,%.4f,%.4f) -> %s\n",
                  got[0].Position.x, got[0].Position.y, got[0].Position.z, got[0].Rotation.x,
                  got[0].Rotation.y, got[0].Rotation.z, got[0].Rotation.w, pass ? "PASS" : "FAIL");
    else
      std::printf("[selftest-samplecpupoints] A(mid) size=%zu -> %s\n", got.size(), pass ? "PASS" : "FAIL");
  }

  // CASE B — coincident keys (posA==posB) → l<=eps branch → output = key a verbatim (cs:44-48). Distinct
  // FX1/Color so the "copy a whole" is asserted (NOT a freshly-seeded point). injectBug drops it → RED.
  {
    SwPoint a = mkKey(3, 4, 5, 0, 0, 0, 1);
    a.FX1 = 9.0f;
    a.Color = {0.2f, 0.3f, 0.4f, 0.5f};
    std::vector<SwPoint> in = {a, a};  // identical positions → d=0 → coincident branch
    std::vector<SwPoint> got = runSample(in, 0.5f, 1.0f);
    bool pass = got.size() == 1 && near1(got[0].Position.x, 3.0f) && near1(got[0].Position.y, 4.0f) &&
                near1(got[0].Position.z, 5.0f) && near1(got[0].FX1, 9.0f) &&
                near1(got[0].Color.x, 0.2f);
    ok = ok && pass;
    std::printf("[selftest-samplecpupoints] B(coincident copy-a) size=%zu -> %s\n", got.size(),
                pass ? "PASS" : "FAIL");
  }

  // CASE C — SamplePos clamp: SamplePos=99 with N=2 → f clamps to 1 → i0=1,i1=1,t=0. l=0 (a==a positions
  // distinct? no: i0==i1 → posA==posB → coincident branch → output = in[1] verbatim). Asserts the upper clamp.
  {
    std::vector<SwPoint> in = {mkKey(0, 0, 0, 0, 0, 0, 1), mkKey(7, 0, 0, 0, 0, 0, 1)};
    std::vector<SwPoint> got = runSample(in, 99.0f, 0.0f);
    bool pass = got.size() == 1 && near1(got[0].Position.x, 7.0f);  // clamped to last key
    ok = ok && pass;
    std::printf("[selftest-samplecpupoints] C(clamp-hi) pos.x=%.4f -> %s\n",
                got.empty() ? -1.0f : got[0].Position.x, pass ? "PASS" : "FAIL");
  }

  // CASE D — P2 fix: TangentScale=1, NON-coincident keys, mid t (Bezier control points OFF the segment).
  // Keys: a=(0,0,0) identity, b=(2,0,0) identity. SamplePos=0.25 → f=0.25, i0=0, t=0.25 (cs:33-38).
  // Hand-derivation (SampleCpuPoints.cs + Bezier.cs, verbatim):
  //   l = |posB-posA| = 2; tLength = TangentScale*l = 1*2 = 2                          (cs:43,52)
  //   tA = Transform(+UnitZ*2, identity) = (0,0,2); tB = Transform(-UnitZ*2, identity) = (0,0,-2) (cs:53-54)
  //   ctrl: p0=(0,0,0), p1=posA+tA=(0,0,2), p2=posB+tB=(2,0,-2), p3=(2,0,0)            (cs:56)
  //   pos = Bezier.GetPoint(p0,p1,p2,p3,0.25)  (Bezier.cs:22-31, u=0.75):
  //     x = 3*u*t^2*(p2.x) + t^3*(p3.x) = 3*.75*.0625*2 + .015625*2 = 0.28125+0.03125 = 0.3125
  //     z = 3*u^2*t*(p1.z) + 3*u*t^2*(p2.z) = 3*.5625*.25*2 - 3*.75*.0625*2 = 0.84375-0.28125 = 0.5625
  //   (collapsed TangentScale=0 curve gives z=0 here — the tangent formula is the ONLY source of z≠0)
  //   tan = Bezier.GetFirstDerivative(p0,p1,p2,p3,0.25) (Bezier.cs:33-41)
  //       = 3*.5625*(0,0,2) + 6*.75*.25*(2,0,-4) + 3*.0625*(0,0,2) = (2.25, 0, -0.75)
  //   ComputeOrientation(id,id,tan,.25) (cs:136-166): f=norm(tan)=(3,0,-1)/√10; upA=upB=(0,1,0) →
  //     slerp dot=1 → up=(0,1,0) (cs:174-175); up⊥f already; right=norm(up×f)=(-1,0,-3)/√10 (cs:156);
  //     up=f×right=(0,1,0) (cs:157). trace=2*(-1/√10)+1 > 0 → w=sqrt(trace+1)/2, y=(m31-m13)*0.5/sqrt(trace+1).
  //   python (formulas above): w=math.sqrt(2-2/math.sqrt(10))/2 = 0.58471028;
  //     y=(2*3/math.sqrt(10))*0.5/math.sqrt(2-2/math.sqrt(10)) = 0.81124219; x=z=0.
  //   EXPECTED: Position=(0.3125, 0, 0.5625), Orientation=(0, 0.81124219, 0, 0.58471028).
  {
    std::vector<SwPoint> in = {mkKey(0, 0, 0, 0, 0, 0, 1), mkKey(2, 0, 0, 0, 0, 0, 1)};
    std::vector<SwPoint> got = runSample(in, 0.25f, 1.0f);
    bool pass = got.size() == 1 && near1(got[0].Position.x, 0.3125f) && near1(got[0].Position.y, 0.0f) &&
                near1(got[0].Position.z, 0.5625f) && near1(got[0].Rotation.x, 0.0f) &&
                near1(got[0].Rotation.y, 0.81124219f) && near1(got[0].Rotation.z, 0.0f) &&
                near1(got[0].Rotation.w, 0.58471028f);
    ok = ok && pass;
    if (got.size() == 1)
      std::printf("[selftest-samplecpupoints] D(tangent-mid) pos=(%.4f,%.4f,%.4f) rot=(%.4f,%.4f,%.4f,%.4f) -> %s\n",
                  got[0].Position.x, got[0].Position.y, got[0].Position.z, got[0].Rotation.x,
                  got[0].Rotation.y, got[0].Rotation.z, got[0].Rotation.w, pass ? "PASS" : "FAIL");
    else
      std::printf("[selftest-samplecpupoints] D(tangent-mid) size=%zu -> %s\n", got.size(),
                  pass ? "PASS" : "FAIL");
  }

  // CASE E — P2 fix: key b ROTATED +90° about X (qb=(√2/2,0,0,√2/2)) so BOTH quaternion transforms fire:
  //   tB = Transform(-UnitZ*2, qb) = (0, 2, 0)   (z→-y under +90°X, so -z→+y; cs:54)
  //   upB = Transform(UnitY, qb) = (0, 0, 1)     (y→z; cs:144) → dot(upA,upB)=0 → the REAL slerp branch
  //   (cs:187-189), not the dot>0.9995 nlerp shortcut Case D takes.
  // Keys: a=(0,0,0) identity, b=(2,0,0) qb. SamplePos=0.25, TangentScale=1 → t=0.25, tLength=2.
  //   ctrl: p1=(0,0,2), p2=posB+tB=(2,2,0)
  //   pos = Bezier(p0,p1,p2,p3,0.25): x=0.3125 (same as D); y=3*.75*.0625*2=0.28125;
  //     z=3*.5625*.25*2=0.84375                                     → (0.3125, 0.28125, 0.84375)
  //   tan = 3*.5625*(0,0,2)+6*.1875*(2,2,-2)+3*.0625*(0,-2,0) = (2.25, 1.875, 1.125)
  //   ComputeOrientation(cs:136-166) with slerp(upA=(0,1,0), upB=(0,0,1), t=.25): theta=π/2 →
  //     up=(0, sin(.75·π/2), sin(.25·π/2)) = (0, 0.92388, 0.38268), then ⊥f + basis + matrix→quat.
  //   python (SampleCpuPoints.cs:136-190 + System.Numerics CreateFromRotationMatrix transcription):
  //     f=norm((2.25,1.875,1.125)); up=slerp above; up-=f*dot(up,f); up=norm(up);
  //     right=norm(cross(up,f)); up=cross(f,right); trace>0 branch →
  //   EXPECTED: Position=(0.3125, 0.28125, 0.84375),
  //             Orientation=(-0.13818691, 0.54919950, 0.35718628, 0.74276662).
  {
    std::vector<SwPoint> in = {mkKey(0, 0, 0, 0, 0, 0, 1), mkKey(2, 0, 0, r2, 0, 0, r2)};
    std::vector<SwPoint> got = runSample(in, 0.25f, 1.0f);
    bool pass = got.size() == 1 && near1(got[0].Position.x, 0.3125f) &&
                near1(got[0].Position.y, 0.28125f) && near1(got[0].Position.z, 0.84375f) &&
                near1(got[0].Rotation.x, -0.13818691f) && near1(got[0].Rotation.y, 0.54919950f) &&
                near1(got[0].Rotation.z, 0.35718628f) && near1(got[0].Rotation.w, 0.74276662f);
    ok = ok && pass;
    if (got.size() == 1)
      std::printf("[selftest-samplecpupoints] E(rot-key-slerp) pos=(%.4f,%.4f,%.4f) rot=(%.4f,%.4f,%.4f,%.4f) -> %s\n",
                  got[0].Position.x, got[0].Position.y, got[0].Position.z, got[0].Rotation.x,
                  got[0].Rotation.y, got[0].Rotation.z, got[0].Rotation.w, pass ? "PASS" : "FAIL");
    else
      std::printf("[selftest-samplecpupoints] E(rot-key-slerp) size=%zu -> %s\n", got.size(),
                  pass ? "PASS" : "FAIL");
  }

  pointListInjectBug() = false;
  std::printf("[selftest-samplecpupoints] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
