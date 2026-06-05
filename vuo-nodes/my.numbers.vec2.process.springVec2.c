/**
 * @file
 * my.numbers.vec2.process.springVec2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SpringVec2
 * - Category: Operators/Lib/numbers/vec2/process
 * - Source: external/tixl/Operators/Lib/numbers/vec2/process/SpringVec2.cs
 * - Primary output(s): VuoPoint2d Result (ColorForValues #868C8D)
 *
 * bounded vector spring adapter. ProofValue is a Vuo-only numeric tap for the Batch 50 proof composition.
 */


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


VuoModuleMetadata({
					 "title" : "my_SpringVec2",
					 "description" : "TiXL SpringVec2 bounded vector spring adapter. Source: external/tixl/Operators/Lib/numbers/vec2/process/SpringVec2.cs. Category: Operators/Lib/numbers/vec2/process. Primary output(s): VuoPoint2d Result (ColorForValues #868C8D). ProofValue is Vuo-only.",
					 "keywords" : [ "tixl", "numbers", "ColorForValues", "#868C8D", "batch50" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal, {"default":0.35}) timeValue,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":1.0}) rate,
		VuoInputData(VuoReal, {"default":1.0}) speedFactor,
		VuoInputData(VuoReal, {"default":1.0}) amplitude,
		VuoInputData(VuoReal, {"default":0.0}) bias,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoInputData(VuoReal, {"default":0.0}) base,
		VuoInputData(VuoReal, {"default":0.0}) delay,
		VuoInputData(VuoReal, {"default":1.0}) duration,
		VuoInputData(VuoReal, {"default":0.5}) f,
		VuoInputData(VuoReal, {"default":0.25}) damping,
		VuoInputData(VuoReal, {"default":1.0}) factor,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoReal, {"default":0.1}) strength,
		VuoInputData(VuoReal, {"default":1.0}) tension,
		VuoInputData(VuoReal, {"default":0.001}) threshold,
		VuoInputData(VuoReal, {"name":"Min","default":0.0}) minValue,
		VuoInputData(VuoReal, {"name":"Max","default":1.0}) maxValue,
		VuoInputData(VuoReal, {"default":1.0}) frequency,
		VuoInputData(VuoReal, {"default":0.0}) angleInput,
		VuoInputData(VuoReal, {"default":1.0}) r,
		VuoInputData(VuoReal, {"default":0.5}) g,
		VuoInputData(VuoReal, {"default":0.25}) b,
		VuoInputData(VuoReal, {"default":1.0}) a,
		VuoInputData(VuoInteger, {"default":0}) direction,
		VuoInputData(VuoInteger, {"default":1}) interpolation,
		VuoInputData(VuoInteger, {"default":0}) mode,
		VuoInputData(VuoInteger, {"default":4}) modulo,
		VuoInputData(VuoInteger, {"default":7}) seed,
		VuoInputData(VuoInteger, {"default":1}) index,
		VuoInputData(VuoBoolean, {"default":true}) trigger,
		VuoInputData(VuoBoolean, {"default":false}) clampInput,
		VuoInputData(VuoBoolean, {"default":true}) showGizmo,
		VuoInputData(VuoPoint2d, {"default":{"x":0.25,"y":0.75}}) input2d,
		VuoInputData(VuoPoint3d, {"default":{"x":0.25,"y":0.5,"z":0.75}}) vectorA,
		VuoInputData(VuoPoint3d, {"default":{"x":0.7,"y":0.2,"z":0.4}}) vectorB,
		VuoInputData(VuoPoint3d, {"default":{"x":0.0,"y":1.0,"z":0.0}}) axisInput,
		VuoInputData(VuoPoint3d, {"default":{"x":0.1,"y":0.2,"z":0.3}}) rotation,
		VuoInputData(VuoPoint3d, {"default":{"x":0.1,"y":0.1,"z":0.1}}) precision,
		VuoInputData(VuoPoint4d, {"default":{"x":0.1,"y":0.2,"z":0.3,"w":1.0}}) vector4A,
		VuoInputData(VuoPoint4d, {"default":{"x":0.8,"y":0.4,"z":0.2,"w":1.0}}) vector4B,
		VuoOutputData(VuoPoint2d, {"name":"Result"}) result,
		VuoOutputData(VuoReal, {"name":"ProofValue"}) proofValue
)
{
	VuoReal proof = 0.0;
	(void)renderTick; (void)direction; (void)mode; (void)minValue; (void)maxValue; (void)trigger; (void)clampInput; (void)showGizmo;
	VuoPoint2d target = VuoPoint2d_make(input2d.y, input2d.x); *result = VuoPoint2d_make(input2d.x + (target.x - input2d.x) * clampReal(strength / fmax(0.0001, tension), 0.0, 1.0), input2d.y + (target.y - input2d.y) * clampReal(strength / fmax(0.0001, tension), 0.0, 1.0)); proof = length2(*result);
	*proofValue = proof;
}
