// runtime/spatial_audio — the CLOSED-FORM math of the SpatialAudioPlayer operator (TiXL
// Operators/Lib/io/audio/SpatialAudioPlayer.cs + Core/Audio/SpatialOperatorAudioStream.cs). Pure geometry
// → gain: distance attenuation, effective volume, and the listener/source Euler→direction vectors. These
// are the parts sw can prove against TiXL WITHOUT running BASS's 3D sound field (which computes the true
// per-ear pan/HRTF inside the audio card). The real spatialized output to the speakers is a platform
// concern (AVAudioEnvironmentNode) marked deferred-hw-verify; THIS leaf is the value-exact spine.
//
// TiXL AUTHORITY (file:line at each fn). Two clamp layers, both ported:
//   OPERATOR layer (SpatialAudioPlayer.cs:192-211): minDistance<=0 → 1.0; maxDistance<=minDistance →
//     minDistance+10; cone angles clamp [0,360], outerConeVolume clamp [0,1], Audio3DMode clamp [0,2].
//   STREAM layer (SpatialOperatorAudioStream.cs:247-269, 439-460): minDistance=max(0.1,minDistance);
//     maxDistance=max(minDistance+0.1,maxDistance); linear attenuation 1@min..0@max; effective volume =
//     muted/beyond-max ? 0 : currentVolume * distanceAttenuation.
//
// runtime leaf: pure computation, no hardware / no UI / no upward dep.
#pragma once
#include <simd/simd.h>

namespace sw {

// Listener/source orientation from Euler angles in DEGREES (SpatialAudioPlayer.cs:185-206). TiXL builds
// Matrix4x4.CreateFromYawPitchRoll(Y·(pi/180), X·(pi/180), Z·(pi/180)) then Vector3.Transform of the basis
// vector. forwardFromEuler = Transform((0,0,1), M); upFromEuler = Transform((0,1,0), M). The rotation math
// is the .NET Quaternion.CreateFromYawPitchRoll → Matrix4x4.CreateFromQuaternion path (row-vector), the
// SAME verbatim port already used in point_ops_polartransformpoints.
simd::float3 spatialForwardFromEuler(simd::float3 eulerDegrees);  // Transform((0,0,1), R)
simd::float3 spatialUpFromEuler(simd::float3 eulerDegrees);        // Transform((0,1,0), R)

// Linear distance attenuation (SpatialOperatorAudioStream.cs:247-269), with BOTH clamp layers folded in.
// `minDistanceIn`/`maxDistanceIn` are the RAW operator inputs (before any clamp); this applies the
// operator clamp (cs:193-195) THEN the stream clamp (cs:247-248), then the piecewise-linear falloff:
//   dist<=min → 1 ; dist>=max → 0 ; else 1 - (dist-min)/(max-min).
// Returns a value in [0,1]. distanceToListener = Vector3.Distance(sourcePos, listenerPos).
float spatialDistanceAttenuation(float distanceToListener, float minDistanceIn, float maxDistanceIn);

// Effective linear output volume (SpatialOperatorAudioStream.cs:439-460): 0 when muted OR beyond max
// distance (attenuation==0 ⇔ beyond max, given the clamps); else currentVolume * distanceAttenuation.
// `mute` = the operator's Mute input; `attenuation` = spatialDistanceAttenuation() output.
float spatialEffectiveVolume(float currentVolume, float attenuation, bool mute);

// ── Golden TEETH hook (--selftest-spatialaudio-bug). false = production; true = DROP the `1.0 -` falloff
// inversion in spatialDistanceAttenuation (SpatialOperatorAudioStream.cs:269) so near/far attenuation swaps
// (returns t instead of 1-t). The golden's diverging-middle probe (0.5 at the range midpoint) stays 0.5
// under the swap symmetrically, so the golden also probes an OFF-CENTER point (t=0.25 → 0.75 vs bugged
// 0.25) to catch it. Rots the REAL math (not a want-flip). Sticky; the golden restores false.
void setSpatialAudioBug(bool on);
bool spatialAudioBug();

// Isolated proof (--selftest-spatialaudio): the distance falloff hits 1 at/inside min, 0 at/beyond max,
// and the exact linear midpoint between; effective volume gates on mute + beyond-max and scales by
// attenuation; the Euler→forward/up vectors match hand-derived rotations. injectBug corrupts the REAL
// math so the test must FAIL. Returns 0 on PASS, 1 on FAIL (0 on a did-not-trip bug leg — dead tooth).
int runSpatialAudioSelfTest(bool injectBug);

}  // namespace sw
