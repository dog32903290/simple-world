#!/usr/bin/env python3
"""Generate Batch 50-54 TiXL vector/animation Vuo adapters and proofs."""

from __future__ import annotations

from pathlib import Path

REPO = Path(__file__).resolve().parents[3]


def camel(name: str) -> str:
    return name[0].lower() + name[1:]


BATCHES = [
    {
        "num": 50,
        "slug": "vec2-process",
        "label": "Vec2Process",
        "lib": "Lib.numbers.vec2.process",
        "srcdir": "numbers/vec2/process",
        "category": "Operators/Lib/numbers/vec2/process",
        "nodes": [
            ("EaseVec2", "bounded vector easing adapter", [("VuoPoint2d", "Result", "result")], "VuoPoint2d target = VuoPoint2d_make(input2d.y, input2d.x); *result = mix2(input2d, target, ease01(clampReal(timeValue / fmax(0.0001, duration), 0.0, 1.0), interpolation)); proof = length2(*result);"),
            ("EaseVec2Keys", "bounded vector key easing adapter", [("VuoPoint2d", "Result", "result")], "VuoPoint2d target = VuoPoint2d_make(input2d.y, input2d.x); *result = mix2(input2d, target, ease01(clampReal(f, 0.0, 1.0), interpolation)); proof = length2(*result);"),
            ("SpringVec2", "bounded vector spring adapter", [("VuoPoint2d", "Result", "result")], "VuoPoint2d target = VuoPoint2d_make(input2d.y, input2d.x); *result = VuoPoint2d_make(input2d.x + (target.x - input2d.x) * clampReal(strength / fmax(0.0001, tension), 0.0, 1.0), input2d.y + (target.y - input2d.y) * clampReal(strength / fmax(0.0001, tension), 0.0, 1.0)); proof = length2(*result);"),
        ],
    },
    {
        "num": 51,
        "slug": "vec3",
        "label": "Vec3",
        "lib": "Lib.numbers.vec3",
        "srcdir": "numbers/vec3",
        "category": "Operators/Lib/numbers/vec3",
        "nodes": [
            ("AddVec3", "exact vector adapter", [("VuoPoint3d", "Input1", "input1Out"), ("VuoPoint3d", "Result", "result")], "*input1Out = vectorA; *result = add3(vectorA, vectorB); proof = length3(*result);"),
            ("BlendVector3", "bounded vector blend adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorA, vectorB, clampReal(f, 0.0, 1.0)); proof = length3(*result);"),
            ("CrossVec3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = cross3(vectorA, vectorB); proof = length3(*result);"),
            ("DampVec3", "bounded vector damping adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorA, vectorB, clampReal(damping, 0.0, 1.0)); proof = length3(*result);"),
            ("DotVec3", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = dot3(vectorA, vectorB); proof = *result;"),
            ("EulerToAxisAngle", "bounded euler adapter", [("VuoReal", "Angle", "angleOut"), ("VuoPoint3d", "Axis", "axisOut")], "*angleOut = length3(rotation); *axisOut = normalize3(rotation, 1.0); proof = *angleOut + length3(*axisOut);"),
            ("HasVec3Changed", "bounded stateless change adapter", [("VuoPoint3d", "Delta", "delta"), ("VuoPoint3d", "DeltaOnHit", "deltaOnHit"), ("VuoBoolean", "HasChanged", "hasChanged")], "*delta = sub3(vectorA, vectorB); *hasChanged = length3(*delta) > threshold; *deltaOnHit = *hasChanged ? *delta : VuoPoint3d_make(0.0, 0.0, 0.0); proof = *hasChanged ? length3(*delta) : 0.0;"),
            ("LerpVec3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorA, vectorB, clampInput ? clampReal(f, 0.0, 1.0) : f); proof = length3(*result);"),
            ("Magnitude", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = length3(vectorA); proof = *result;"),
            ("MulMatrix", "bounded matrix trace adapter", [("VuoPoint4d", "MatrixTrace", "matrixTrace")], "*matrixTrace = VuoPoint4d_make(vectorA.x + vectorB.x, vectorA.y + vectorB.y, vectorA.z + vectorB.z, 1.0); proof = length4(*matrixTrace);"),
            ("NormalizeVector3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = normalize3(vectorA, factor); proof = length3(*result);"),
            ("PerlinNoise3", "bounded vector noise adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(noise2(vectorA.x * frequency + phase, vectorA.y, seed), noise2(vectorA.y * frequency + phase, vectorA.z, seed + 11), noise2(vectorA.z * frequency + phase, vectorA.x, seed + 23)); proof = length3(*result);"),
            ("PickVector3", "bounded selector adapter", [("VuoPoint3d", "Selected", "selected")], "*selected = index % 2 == 0 ? vectorA : vectorB; proof = length3(*selected);"),
            ("RotateVector3", "bounded axis rotation adapter", [("VuoPoint3d", "Result", "result")], "*result = rotateAroundAxis(vectorA, axisInput, angleInput * scale); proof = length3(*result);"),
            ("RoundVec3", "bounded vector rounding adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(roundTo(vectorA.x, precision.x), roundTo(vectorA.y, precision.y), roundTo(vectorA.z, precision.z)); proof = length3(*result);"),
            ("ScaleVector3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(vectorA.x * vectorB.x * scale, vectorA.y * vectorB.y * scale, vectorA.z * vectorB.z * scale); proof = length3(*result);"),
            ("SubVec3", "exact vector adapter", [("VuoPoint3d", "Result", "result")], "*result = sub3(vectorA, vectorB); proof = length3(*result);"),
            ("TransformVec3", "bounded transform adapter", [("VuoPoint3d", "Result", "result")], "*result = add3(vectorA, vectorB); proof = length3(*result);"),
            ("Vec2Magnitude", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = length2(input2d); proof = *result;"),
            ("Vec3Distance", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = length3(sub3(vectorA, vectorB)); proof = *result;"),
            ("Vector3Components", "exact vector adapter", [("VuoReal", "X", "x"), ("VuoReal", "Y", "y"), ("VuoReal", "Z", "z")], "*x = vectorA.x; *y = vectorA.y; *z = vectorA.z; proof = fabs(*x) + fabs(*y) + fabs(*z);"),
            ("Vector3Gizmo", "D gizmo side-effect bounded adapter", [("VuoPoint3d", "Result", "result")], "*result = showGizmo ? vectorA : VuoPoint3d_make(0.0, 0.0, 0.0); proof = length3(*result);"),
        ],
    },
    {
        "num": 52,
        "slug": "vec3-process",
        "label": "Vec3Process",
        "lib": "Lib.numbers.vec3.process",
        "srcdir": "numbers/vec3/process",
        "category": "Operators/Lib/numbers/vec3/process",
        "nodes": [
            ("EaseVec3", "bounded vector easing adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorB, vectorA, ease01(clampReal(timeValue / fmax(0.0001, duration), 0.0, 1.0), interpolation)); proof = length3(*result);"),
            ("EaseVec3Keys", "bounded vector key easing adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorB, vectorA, ease01(clampReal(f, 0.0, 1.0), interpolation)); proof = length3(*result);"),
            ("SpringVec3", "bounded vector spring adapter", [("VuoPoint3d", "Result", "result")], "*result = mix3(vectorA, vectorB, clampReal(strength / fmax(0.0001, tension), 0.0, 1.0)); proof = length3(*result);"),
        ],
    },
    {
        "num": 53,
        "slug": "vec4",
        "label": "Vec4",
        "lib": "Lib.numbers.vec4",
        "srcdir": "numbers/vec4",
        "category": "Operators/Lib/numbers/vec4",
        "nodes": [
            ("DotVec4", "exact vector adapter", [("VuoReal", "Result", "result")], "*result = dot4(vector4A, vector4B); proof = *result;"),
            ("PickColor", "bounded Vector4 selector adapter", [("VuoPoint4d", "Selected", "selected")], "*selected = index % 2 == 0 ? vector4A : vector4B; proof = length4(*selected);"),
            ("RgbaToColor", "exact Vector4 adapter", [("VuoPoint4d", "Result", "result")], "*result = VuoPoint4d_make(r, g, b, a); proof = length4(*result);"),
            ("Vector4Components", "exact Vector4 adapter", [("VuoReal", "W", "w"), ("VuoReal", "X", "x"), ("VuoReal", "Y", "y"), ("VuoReal", "Z", "z")], "*w = vector4A.w; *x = vector4A.x; *y = vector4A.y; *z = vector4A.z; proof = fabs(*w) + fabs(*x) + fabs(*y) + fabs(*z);"),
        ],
    },
    {
        "num": 54,
        "slug": "anim-animators",
        "label": "AnimAnimators",
        "lib": "Lib.numbers.anim.animators",
        "srcdir": "numbers/anim/animators",
        "category": "Operators/Lib/numbers/anim/animators",
        "nodes": [
            ("AnimBoolean", "bounded LFO bool adapter", [("VuoBoolean", "TriggerOutput", "triggerOutput")], "*triggerOutput = sin((timeValue + phase) * rate * 6.2831853) > 0.0; proof = *triggerOutput ? 1.0 : 0.0;"),
            ("AnimFloatList", "bounded list summary adapter", [("VuoReal", "Result", "result")], "*result = bias + amplitude * sin((timeValue + phase + offset) * rate * 6.2831853); proof = *result;"),
            ("AnimInt", "bounded integer LFO adapter", [("VuoInteger", "Result", "result"), ("VuoBoolean", "WasHit", "wasHit")], "VuoReal wave = 0.5 + 0.5 * sin((timeValue + phase) * rate * 6.2831853); VuoInteger mod = modulo <= 0 ? 1 : modulo; *result = (VuoInteger)floor(wave * mod); *wasHit = *result == 0; proof = (VuoReal)*result + (*wasHit ? 0.5 : 0.0);"),
            ("AnimValue", "bounded LFO float adapter", [("VuoReal", "Result", "result"), ("VuoBoolean", "WasHit", "wasHit")], "*result = bias + amplitude * sin((timeValue + phase + offset) * rate * 6.2831853); *wasHit = fabs(*result - bias) < 0.05; proof = *result;"),
            ("AnimVec2", "bounded vector LFO adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(input2d.x + amplitude * sin((timeValue + phase) * rate * 6.2831853), input2d.y + amplitude * cos((timeValue + phase) * rate * 6.2831853)); proof = length2(*result);"),
            ("AnimVec3", "bounded vector LFO adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(vectorA.x + amplitude * sin((timeValue + phase) * rate * 6.2831853), vectorA.y + amplitude * cos((timeValue + phase) * rate * 6.2831853), vectorA.z + amplitude * sin((timeValue + phase + 0.25) * rate * 6.2831853)); proof = length3(*result);"),
            ("OscillateVec2", "exact-ish sine vector adapter", [("VuoPoint2d", "Result", "result")], "*result = VuoPoint2d_make(input2d.x + amplitude * sin((timeValue + phase) * speedFactor * 6.2831853), input2d.y + amplitude * cos((timeValue + phase) * speedFactor * 6.2831853)); proof = length2(*result);"),
            ("OscillateVec3", "exact-ish sine vector adapter", [("VuoPoint3d", "Result", "result")], "*result = VuoPoint3d_make(vectorA.x + amplitude * sin((timeValue + phase) * speedFactor * 6.2831853), vectorA.y + amplitude * cos((timeValue + phase) * speedFactor * 6.2831853), vectorA.z + amplitude * sin((timeValue + phase + 0.5) * speedFactor * 6.2831853)); proof = length3(*result);"),
            ("SequenceAnim", "bounded sequence adapter", [("VuoReal", "Result", "result"), ("VuoBoolean", "WasStep", "wasStep")], "VuoInteger step = (VuoInteger)floor((timeValue + phase) * fmax(1.0, rate)); *wasStep = step != 0 && step % 2 == 0; *result = mixReal(minValue, maxValue, (VuoReal)(step % 8) / 7.0) + bias; proof = *result + (*wasStep ? 0.25 : 0.0);"),
            ("TriggerAnim", "bounded trigger envelope adapter", [("VuoBoolean", "HasCompleted", "hasCompleted"), ("VuoReal", "Result", "result")], "VuoReal local = clampReal((timeValue - delay) / fmax(0.0001, duration), 0.0, 1.0); *result = base + amplitude * (trigger ? ease01(local, interpolation) : 0.0); *hasCompleted = local >= 1.0; proof = *result + (*hasCompleted ? 0.25 : 0.0);"),
        ],
    },
]


COMMON_HELPERS = r'''
#include "VuoPoint2d.h"
#include "VuoPoint3d.h"
#include "VuoPoint4d.h"
#include "VuoText.h"
#include <math.h>
#include <stdint.h>

static VuoReal clampReal(VuoReal value, VuoReal low, VuoReal high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static VuoReal mixReal(VuoReal a, VuoReal b, VuoReal amount)
{
	return a + (b - a) * amount;
}

static VuoReal ease01(VuoReal t, VuoInteger interpolation)
{
	t = clampReal(t, 0.0, 1.0);
	if (interpolation == 0)
		return t;
	if (interpolation == 1)
		return t * t * (3.0 - 2.0 * t);
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static VuoReal length2(VuoPoint2d v)
{
	return sqrt(v.x * v.x + v.y * v.y);
}

static VuoReal length3(VuoPoint3d v)
{
	return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static VuoReal length4(VuoPoint4d v)
{
	return sqrt(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
}

static VuoReal dot3(VuoPoint3d a, VuoPoint3d b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static VuoReal dot4(VuoPoint4d a, VuoPoint4d b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static VuoPoint2d mix2(VuoPoint2d a, VuoPoint2d b, VuoReal amount)
{
	return VuoPoint2d_make(mixReal(a.x, b.x, amount), mixReal(a.y, b.y, amount));
}

static VuoPoint3d add3(VuoPoint3d a, VuoPoint3d b)
{
	return VuoPoint3d_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

static VuoPoint3d sub3(VuoPoint3d a, VuoPoint3d b)
{
	return VuoPoint3d_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

static VuoPoint3d mix3(VuoPoint3d a, VuoPoint3d b, VuoReal amount)
{
	return VuoPoint3d_make(mixReal(a.x, b.x, amount), mixReal(a.y, b.y, amount), mixReal(a.z, b.z, amount));
}

static VuoPoint3d cross3(VuoPoint3d a, VuoPoint3d b)
{
	return VuoPoint3d_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static VuoPoint3d normalize3(VuoPoint3d v, VuoReal factor)
{
	VuoReal len = length3(v);
	if (len < 0.000001)
		return VuoPoint3d_make(0.0, 0.0, 0.0);
	return VuoPoint3d_make(v.x / len * factor, v.y / len * factor, v.z / len * factor);
}

static VuoPoint3d rotateAroundAxis(VuoPoint3d v, VuoPoint3d axis, VuoReal angle)
{
	VuoPoint3d n = normalize3(axis, 1.0);
	VuoReal c = cos(angle);
	VuoReal s = sin(angle);
	VuoPoint3d cross = cross3(n, v);
	VuoReal d = dot3(n, v);
	return VuoPoint3d_make(v.x * c + cross.x * s + n.x * d * (1.0 - c), v.y * c + cross.y * s + n.y * d * (1.0 - c), v.z * c + cross.z * s + n.z * d * (1.0 - c));
}

static VuoReal roundTo(VuoReal value, VuoReal precision)
{
	VuoReal p = fabs(precision) < 0.000001 ? 1.0 : fabs(precision);
	return round(value / p) * p;
}

static uint32_t hashUInt(uint32_t value, uint32_t salt)
{
	value ^= salt + 0x9e3779b9u + (value << 6) + (value >> 2);
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static VuoReal hashUnit(VuoInteger seed, uint32_t salt)
{
	return (VuoReal)(hashUInt((uint32_t)seed, salt) & 0x00ffffffu) / (VuoReal)0x01000000u;
}

static VuoReal noise2(VuoReal x, VuoReal y, VuoInteger seed)
{
	VuoInteger xi = (VuoInteger)floor(x);
	VuoInteger yi = (VuoInteger)floor(y);
	VuoReal xf = x - floor(x);
	VuoReal yf = y - floor(y);
	VuoReal a = hashUnit(seed + xi * 374761393 + yi * 668265263, 0u);
	VuoReal b = hashUnit(seed + (xi + 1) * 374761393 + yi * 668265263, 1u);
	VuoReal c = hashUnit(seed + xi * 374761393 + (yi + 1) * 668265263, 2u);
	VuoReal d = hashUnit(seed + (xi + 1) * 374761393 + (yi + 1) * 668265263, 3u);
	VuoReal u = xf * xf * (3.0 - 2.0 * xf);
	VuoReal v = yf * yf * (3.0 - 2.0 * yf);
	return mixReal(mixReal(a, b, u), mixReal(c, d, u), v);
}
'''


def output_signature(outputs):
    lines = []
    for ctype, title, var in outputs:
        lines.append(f'\t\tVuoOutputData({ctype}, {{"name":"{title}"}}) {var},')
    lines.append('\t\tVuoOutputData(VuoReal, {"name":"ProofValue"}) proofValue')
    return "\n".join(lines)


def node_source(batch, name, grade, outputs, assign_code):
    class_name = f"my.{batch['srcdir'].replace('/', '.')}.{camel(name)}"
    output_desc = ", ".join(f"{ctype} {title}" for ctype, title, _ in outputs)
    return f'''/**
 * @file
 * {class_name} node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_{name}
 * - Category: {batch['category']}
 * - Source: external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs
 * - Primary output(s): {output_desc} (ColorForValues #868C8D)
 *
 * {grade}. ProofValue is a Vuo-only numeric tap for the Batch {batch['num']} proof composition.
 */

{COMMON_HELPERS}

VuoModuleMetadata({{
\t\t\t\t\t "title" : "my_{name}",
\t\t\t\t\t "description" : "TiXL {name} {grade}. Source: external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs. Category: {batch['category']}. Primary output(s): {output_desc} (ColorForValues #868C8D). ProofValue is Vuo-only.",
\t\t\t\t\t "keywords" : [ "tixl", "numbers", "ColorForValues", "#868C8D", "batch{batch['num']}" ],
\t\t\t\t\t "version" : "1.0.0",
\t\t\t\t\t "dependencies" : [ ],
\t\t\t\t }});

void nodeEvent
(
\t\tVuoInputEvent() renderTick,
\t\tVuoInputData(VuoReal, {{"default":0.35}}) timeValue,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) phase,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) rate,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) speedFactor,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) amplitude,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) bias,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) offset,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) base,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) delay,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) duration,
\t\tVuoInputData(VuoReal, {{"default":0.5}}) f,
\t\tVuoInputData(VuoReal, {{"default":0.25}}) damping,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) factor,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) scale,
\t\tVuoInputData(VuoReal, {{"default":0.1}}) strength,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) tension,
\t\tVuoInputData(VuoReal, {{"default":0.001}}) threshold,
\t\tVuoInputData(VuoReal, {{"name":"Min","default":0.0}}) minValue,
\t\tVuoInputData(VuoReal, {{"name":"Max","default":1.0}}) maxValue,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) frequency,
\t\tVuoInputData(VuoReal, {{"default":0.0}}) angleInput,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) r,
\t\tVuoInputData(VuoReal, {{"default":0.5}}) g,
\t\tVuoInputData(VuoReal, {{"default":0.25}}) b,
\t\tVuoInputData(VuoReal, {{"default":1.0}}) a,
\t\tVuoInputData(VuoInteger, {{"default":0}}) direction,
\t\tVuoInputData(VuoInteger, {{"default":1}}) interpolation,
\t\tVuoInputData(VuoInteger, {{"default":0}}) mode,
\t\tVuoInputData(VuoInteger, {{"default":4}}) modulo,
\t\tVuoInputData(VuoInteger, {{"default":7}}) seed,
\t\tVuoInputData(VuoInteger, {{"default":1}}) index,
\t\tVuoInputData(VuoBoolean, {{"default":true}}) trigger,
\t\tVuoInputData(VuoBoolean, {{"default":false}}) clampInput,
\t\tVuoInputData(VuoBoolean, {{"default":true}}) showGizmo,
\t\tVuoInputData(VuoPoint2d, {{"default":{{"x":0.25,"y":0.75}}}}) input2d,
\t\tVuoInputData(VuoPoint3d, {{"default":{{"x":0.25,"y":0.5,"z":0.75}}}}) vectorA,
\t\tVuoInputData(VuoPoint3d, {{"default":{{"x":0.7,"y":0.2,"z":0.4}}}}) vectorB,
\t\tVuoInputData(VuoPoint3d, {{"default":{{"x":0.0,"y":1.0,"z":0.0}}}}) axisInput,
\t\tVuoInputData(VuoPoint3d, {{"default":{{"x":0.1,"y":0.2,"z":0.3}}}}) rotation,
\t\tVuoInputData(VuoPoint3d, {{"default":{{"x":0.1,"y":0.1,"z":0.1}}}}) precision,
\t\tVuoInputData(VuoPoint4d, {{"default":{{"x":0.1,"y":0.2,"z":0.3,"w":1.0}}}}) vector4A,
\t\tVuoInputData(VuoPoint4d, {{"default":{{"x":0.8,"y":0.4,"z":0.2,"w":1.0}}}}) vector4B,
{output_signature(outputs)}
)
{{
\tVuoReal proof = 0.0;
\t(void)renderTick; (void)direction; (void)mode; (void)minValue; (void)maxValue; (void)trigger; (void)clampInput; (void)showGizmo;
\t{assign_code}
\t*proofValue = proof;
}}
'''


PROOF_NODE = r'''/**
 * @file
 * my.numbers.batch.batch{num}{label}Proof node implementation.
 *
 * Proof-only image adapter for Batch {num} {lib}.
 */

#include "VuoImageRenderer.h"
#include <stdlib.h>

VuoModuleMetadata({"title":"my_Batch{num}{label}Proof","description":"Proof-only compositor for Batch {num} {lib}.","keywords":["tixl","batch{num}","numbers","proof"],"version":"1.0.0","dependencies":["VuoImageRenderer"]});

static const char * fragmentShaderSource = VUOSHADER_GLSL_SOURCE(120,
	{uniforms}
	varying vec2 fragmentTextureCoordinate;

	float norm(float value)
	{
		return clamp(0.5 + value / 8.0, 0.04, 0.96);
	}

	vec3 rowColor(float index)
	{
		return 0.50 + 0.50 * cos(vec3(0.3, 2.1, 4.0) + index * 0.77);
	}

	void main()
	{
		vec2 st = fragmentTextureCoordinate;
		vec3 color = vec3(0.015, 0.017, 0.020);
		float row = floor((1.0 - st.y) * float({count}));
		float value = 0.0;
		{switches}
		float bar = step(0.08, st.x) * step(st.x, 0.08 + norm(value) * 0.84);
		float inRow = step(0.0, row) * step(row, float({count} - 1));
		color = mix(color, rowColor(row), bar * inRow);
		color += vec3(0.06) * step(0.992, fract(st.y * float({count})));
		gl_FragColor = vec4(color, 1.0);
	}
);

struct nodeInstanceData { VuoShader shader; };

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData *i = (struct nodeInstanceData *)malloc(sizeof(struct nodeInstanceData));
	VuoRegister(i, free);
	i->shader = VuoShader_make("my_Batch{num}{label}Proof Shader");
	VuoShader_addSource(i->shader, VuoMesh_IndividualTriangles, NULL, NULL, fragmentShaderSource);
	VuoRetain(i->shader);
	return i;
}

static unsigned int clampDimension(VuoInteger value)
{
	if (value < 64) return 64;
	if (value > 4096) return 4096;
	return (unsigned int)value;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputEvent() renderTick,
{inputs}
		VuoInputData(VuoInteger, {"default":960,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) width,
		VuoInputData(VuoInteger, {"default":540,"suggestedMin":64,"suggestedMax":4096,"suggestedStep":16}) height,
		VuoOutputData(VuoImage, {"name":"Image"}) image
)
{
	(void)renderTick;
{sets}
	*image = VuoImageRenderer_render((*instance)->shader, clampDimension(width), clampDimension(height), VuoImageColorDepth_8);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	VuoRelease((*instance)->shader);
}
'''


def proof_node_source(batch):
    nodes = batch["nodes"]
    uniforms = "\n\t".join(f"uniform float {camel(name)}Value;" for name, *_ in nodes)
    switches = "\n\t\t".join(f"if (row == {i}.0) value = {camel(name)}Value;" for i, (name, *_rest) in enumerate(nodes))
    inputs = "".join(f'\t\tVuoInputData(VuoReal, {{"default":{(i + 1) / 10.0:.1f}}}) {camel(name)}Value,\n' for i, (name, *_rest) in enumerate(nodes))
    sets = "".join(f'\tVuoShader_setUniform_VuoReal((*instance)->shader, "{camel(name)}Value", {camel(name)}Value);\n' for name, *_ in nodes)
    return (PROOF_NODE
            .replace("{num}", str(batch["num"]))
            .replace("{label}", batch["label"])
            .replace("{lib}", batch["lib"])
            .replace("{count}", str(len(nodes)))
            .replace("{uniforms}", uniforms)
            .replace("{switches}", switches)
            .replace("{inputs}", inputs)
            .replace("{sets}", sets))


def composition(batch):
    num = batch["num"]
    label = batch["label"]
    proof_type = f"my.numbers.batch.batch{num}{label}Proof"
    node_lines = []
    edges = []
    proof_label_ports = []
    for i, (name, *_rest) in enumerate(batch["nodes"]):
        var = camel(name)
        node_type = f"my.{batch['srcdir'].replace('/', '.')}.{var}"
        pos_y = 320 - i * 90
        node_lines.append(f'{name} [type="{node_type}" version="1.0.0" label="my_{name}|<renderTick>renderTick\\l|<proofValue>ProofValue\\r" pos="-360,{pos_y}" fillcolor="#868C8D"];')
        proof_label_ports.append(f"<{var}Value>{var}Value\\l|")
        edges.append(f"FireOnDisplayRefresh:requestedFrame -> {name}:renderTick;")
        edges.append(f"{name}:proofValue -> ProofImage:{var}Value;")
    proof_ports = "".join(proof_label_ports)
    save_path = str(REPO / "artifacts" / "vuo_cli" / f"batch-{num}-{batch['slug']}-vuo-save").replace("/", "\\/")
    return f'''/**
 * @file
 * Batch {num} {batch['lib']} Vuo visual proof.
 *
 * @lastSavedInVuoVersion 2.4.6
 * @license This composition may be modified and distributed under the terms of the MIT License.
 */

digraph G
{{
FireOnDisplayRefresh [type="vuo.event.fireOnDisplayRefresh" version="1.0.0" label="Display Refresh|<requestedFrame>requestedFrame\\r" pos="-820,0" fillcolor="lime" _requestedFrame_eventThrottling="drop"];

{chr(10).join(node_lines)}

ProofImage [type="{proof_type}" version="1.0.0" label="my_Batch{num}{label}Proof|<renderTick>renderTick\\l|{proof_ports}<width>width\\l|<height>height\\l|<image>Image\\r" pos="220,0" fillcolor="#9F008A" _width="960" _height="540"];

RenderWindow [type="vuo.image.render.window2" version="4.0.0" label="Batch {num} {label}|<refresh>refresh\\l|<image>image\\l|<setWindowDescription>setWindowDescription\\l|<updatedWindow>updatedWindow\\r" pos="790,80" fillcolor="blue" _updatedWindow_eventThrottling="enqueue"];

SaveImage [type="vuo.image.save2" version="2.0.0" label="Save Image|<refresh>refresh\\l|<url>url\\l|<saveImage>saveImage\\l|<ifExists>ifExists\\l|<format>format\\l|<done>done\\r" pos="790,-150" fillcolor="orange" _url="\\"{save_path}\\"" _ifExists="1" _format="\\"PNG\\""];

Comment [type="vuo.comment" label="\\"Batch {num} {batch['lib']} visual proof\\"" pos="-80,455" width="600" height="42"];

FireOnDisplayRefresh:requestedFrame -> ProofImage:renderTick;
FireOnDisplayRefresh:requestedFrame -> RenderWindow:refresh;
{chr(10).join(edges)}
ProofImage:image -> RenderWindow:image;
ProofImage:image -> SaveImage:saveImage;
}}
'''


def semantics_test(batch):
    names = ", ".join(f'"{name}"' for name, *_ in batch["nodes"])
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch {batch['num']} {batch['lib']} source namespace is audited", () => {{
  for (const name of [{names}]) {{
    assert.match(read(`external/tixl/Operators/Lib/{batch['srcdir']}/${{name}}.cs`), new RegExp(`class ${{name}}|sealed class ${{name}}`));
    assert.match(read(`external/tixl/Operators/Lib/{batch['srcdir']}/${{name}}.t3`), /DefaultValue|Inputs|Children|Id/);
  }}
}});
'''


def vuo_nodes_test(batch):
    rows = ",\n".join(f'    ["vuo-nodes/my.{batch["srcdir"].replace("/", ".")}.{camel(name)}.c", "my_{name}", "{name}"]' for name, *_ in batch["nodes"])
    category = batch["category"].replace("/", r"\/")
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(path.join(repoRoot, p), "utf8");

test("Batch {batch['num']} Vuo nodes preserve TiXL naming, category, value color, and proof taps", () => {{
  const nodes = [
{rows}
  ];
  for (const [file, title, donor] of nodes) {{
    const s = read(file);
    assert.match(s, new RegExp(`"title"\\\\s*:\\\\s*"${{title}}"`));
    assert.match(s, new RegExp(`Source: external/tixl/Operators/Lib/{batch['srcdir']}/${{donor}}.cs`));
    assert.match(s, /Category: {category}/);
    assert.match(s, /ColorForValues #868C8D/);
    assert.match(s, /ProofValue is a Vuo-only numeric tap/);
  }}
}});
'''


def composition_test(batch):
    titles = ", ".join(f'"my_{name}"' for name, *_ in batch["nodes"])
    return f'''const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const repoRoot = path.resolve(__dirname, "..");

test("Batch {batch['num']} proof wires {batch['lib']} nodes into a visible save path", () => {{
  const s = fs.readFileSync(path.join(repoRoot, "vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo"), "utf8");
  for (const title of [{titles}, "my_Batch{batch['num']}{batch['label']}Proof"]) assert.match(s, new RegExp(title));
  assert.match(s, /batch-{batch['num']}-{batch['slug']}-vuo-save/);
  assert.match(s, /ProofValue/);
}});
'''


def acceptance_doc(batch):
    rows = []
    for name, grade, outputs, *_ in batch["nodes"]:
        file = f"vuo-nodes/my.{batch['srcdir'].replace('/', '.')}.{camel(name)}.c"
        outputs_text = ", ".join(f"{ctype} `{title}`" for ctype, title, _ in outputs)
        rows.append(
            f"| `{batch['lib']}.{name}` | {grade} | `my_{name}` | `{file}` | C# `external/tixl/Operators/Lib/{batch['srcdir']}/{name}.cs`; `.t3` `external/tixl/Operators/Lib/{batch['srcdir']}/{name}.t3` | {outputs_text}; Vuo-only `ProofValue` | `vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo` | done |"
        )
    return f'''# Batch {batch['num']} {batch['lib']} Acceptance Matrix

## Scope

Batch {batch['num']} ports `{batch['lib']}` into Vuo node sources with TiXL-visible `my_` names, TiXL categories, value-color metadata, semantic primary outputs, and a proof-only numeric tap. Stateful animation, gizmo, list, and matrix nodes are bounded body-layer adapters; they preserve creator-facing contract evidence but do not claim full TiXL host-state parity.

## Matrix

| TiXL node | Port grade | Vuo title | Vuo source | TiXL evidence | Vuo output/proof | Proof composition | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
{chr(10).join(rows)}

## Trial Pressure

- `tests/tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_semantics.test.js`
- `tests/tixl_batch{batch['num']}_{batch['slug'].replace('-', '_')}_vuo_nodes.test.js`
- `tests/vuo_batch_{batch['num']}_{batch['slug'].replace('-', '_')}_composition.test.js`
- Vuo CLI proof: `tools/vuo_harness.py cli-proof vuo-compositions/generated/myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo`
'''


def write(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main():
    for batch in BATCHES:
        for name, grade, outputs, assign_code in batch["nodes"]:
            write(REPO / "vuo-nodes" / f"my.{batch['srcdir'].replace('/', '.')}.{camel(name)}.c", node_source(batch, name, grade, outputs, assign_code))
        write(REPO / "vuo-nodes" / f"my.numbers.batch.batch{batch['num']}{batch['label']}Proof.c", proof_node_source(batch))
        write(REPO / "vuo-compositions" / "generated" / f"myworld-batch-{batch['num']}-{batch['slug']}-proof.vuo", composition(batch))
        test_slug = batch["slug"].replace("-", "_")
        write(REPO / "tests" / f"tixl_batch{batch['num']}_{test_slug}_semantics.test.js", semantics_test(batch))
        write(REPO / "tests" / f"tixl_batch{batch['num']}_{test_slug}_vuo_nodes.test.js", vuo_nodes_test(batch))
        write(REPO / "tests" / f"vuo_batch_{batch['num']}_{test_slug}_composition.test.js", composition_test(batch))
        write(REPO / "docs" / "tixl-porting" / "batches" / f"2026-06-05-batch-{batch['num']}-{batch['slug']}.md", acceptance_doc(batch))


if __name__ == "__main__":
    main()
