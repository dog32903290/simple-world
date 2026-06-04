/**
 * @file
 * my.numbers.float.adjust.round node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/Round.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Round",
					 "description" : "Exact TiXL Round scalar adapter using RoundValue2. Source: external/tixl/Operators/Lib/numbers/float/adjust/Round.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "round", "steps", "ratio", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myRoundValue2(VuoReal i, VuoReal stepsPerUnit, VuoReal stepRatio)
{
	VuoReal u = 1.0 / stepsPerUnit;
	VuoReal v = stepRatio / (2.0 * stepsPerUnit);
	VuoReal m = fmod(i, u);
	VuoReal r = m - (m < v
					 ? 0.0
					 : m > u - v
					   ? u
					   : (m - v) / (1.0 - 2.0 * stepsPerUnit * v));
	VuoReal y = i - r;
	return y;
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoReal, {"default":1.0}) stepsPerUnit,
		VuoInputData(VuoReal, {"default":0.0}) roundRatio,
		VuoOutputData(VuoReal) result
)
{
	*result = myRoundValue2(value, stepsPerUnit, roundRatio);
}
