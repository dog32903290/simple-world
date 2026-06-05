/**
 * @file
 * my.numbers.float.process.smoothStep node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/process/SmoothStep.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_SmoothStep",
					 "description" : "TiXL SmoothStep scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/process/SmoothStep.cs. Category: Operators/Lib/numbers/float/process. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "process", "smoothstep", "smootherstep", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myClamp01(VuoReal value)
{
	return fmin(fmax(value, 0.0), 1.0);
}

static VuoReal mySmootherStep(VuoReal min, VuoReal max, VuoReal value)
{
	VuoReal t = myClamp01((value - min) / (max - min));
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) min,
		VuoInputData(VuoReal, {"default":1.0}) max,
		VuoInputData(VuoReal, {"default":1.0}) value,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = mySmootherStep(min, max, value);
}
