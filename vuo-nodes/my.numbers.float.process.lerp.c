/**
 * @file
 * my.numbers.float.process.lerp node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/process/Lerp.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Lerp",
					 "description" : "TiXL Lerp scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/process/Lerp.cs. Category: Operators/Lib/numbers/float/process. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "process", "lerp", "linear", "interpolate", "clamp", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myClamp(VuoReal value, VuoReal min, VuoReal max)
{
	return fmin(fmax(value, min), max);
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) a,
		VuoInputData(VuoReal, {"default":1.0}) b,
		VuoInputData(VuoReal, {"default":0.0}) f,
		VuoInputData(VuoBoolean, {"default":false}) clamp,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal factor = f;
	if (clamp)
		factor = myClamp(factor, 0.0, 1.0);

	*result = a + (b - a) * factor;
}
