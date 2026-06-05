/**
 * @file
 * my.numbers.vec2.scaleVector2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ScaleVector2
 * - Category: Operators/Lib/numbers/vec2
 * - Source: external/tixl/Operators/Lib/numbers/vec2/ScaleVector2.cs
 * - Primary output(s): VuoPoint2d Result (ColorForValues #868C8D)
 *
 * exact vector adapter. ProofValue is a Vuo-only numeric tap for the Batch 49 proof composition.
 */


#include "VuoPoint2d.h"
#include "VuoPoint3d.h"
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

static VuoReal safeDiv(VuoReal a, VuoReal b)
{
	return fabs(b) < 0.000001 ? 0.0 : a / b;
}

static VuoReal length2(VuoPoint2d v)
{
	return sqrt(v.x * v.x + v.y * v.y);
}

static VuoPoint2d mix2(VuoPoint2d a, VuoPoint2d b, VuoReal amount)
{
	return VuoPoint2d_make(mixReal(a.x, b.x, amount), mixReal(a.y, b.y, amount));
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
					 "title" : "my_ScaleVector2",
					 "description" : "TiXL ScaleVector2 exact vector adapter. Source: external/tixl/Operators/Lib/numbers/vec2/ScaleVector2.cs. Category: Operators/Lib/numbers/vec2. Primary output(s): VuoPoint2d Result (ColorForValues #868C8D). ProofValue is Vuo-only.",
					 "keywords" : [ "tixl", "numbers", "ColorForValues", "#868C8D", "batch49" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputEvent() renderTick,
		VuoInputData(VuoReal, {"default":0.35}) timeValue,
		VuoInputData(VuoReal, {"default":0.1}) a,
		VuoInputData(VuoReal, {"default":1.0}) b,
		VuoInputData(VuoReal, {"name":"Min","default":0.0}) minValue,
		VuoInputData(VuoReal, {"name":"Max","default":1.0}) maxValue,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoInputData(VuoReal, {"default":1.0}) speedFactor,
		VuoInputData(VuoReal, {"default":0.25}) damping,
		VuoInputData(VuoReal, {"default":0.0}) delayDuration,
		VuoInputData(VuoReal, {"default":0.0}) seedReal,
		VuoInputData(VuoInteger, {"default":0}) mode,
		VuoInputData(VuoInteger, {"default":0}) timeMode,
		VuoInputData(VuoInteger, {"default":7}) seed,
		VuoInputData(VuoInteger, {"default":4}) frameCount,
		VuoInputData(VuoInteger, {"default":1}) index,
		VuoInputData(VuoBoolean, {"default":true}) trigger,
		VuoInputData(VuoBoolean, {"default":false}) value,
		VuoInputData(VuoBoolean, {"default":false}) freeze,
		VuoInputData(VuoBoolean, {"default":false}) uniqueForChild,
		VuoInputData(VuoPoint2d, {"default":{"x":0.25,"y":0.75}}) input1,
		VuoInputData(VuoPoint2d, {"default":{"x":0.5,"y":0.2}}) input2,
		VuoOutputData(VuoPoint2d, {"name":"Result"}) result,
		VuoOutputData(VuoReal, {"name":"ProofValue"}) proofValue
)
{
	VuoReal proof = 0.0;
	(void)renderTick; (void)mode; (void)timeMode; (void)freeze; (void)uniqueForChild;
	*result = VuoPoint2d_make(input1.x * scale, input1.y * scale); proof = length2(*result);
	*proofValue = proof;
}
