/**
 * @file
 * my.numbers.float.trigonometry.sin node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/trigonometry/Sin.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Sin",
					 "description" : "TiXL Sin scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/trigonometry/Sin.cs. Category: Operators/Lib/numbers/float/trigonometry. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "trigonometry", "sin", "radians", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) input,
		VuoInputData(VuoReal, {"default":1.0}) period,
		VuoInputData(VuoReal, {"default":0.0}) phase,
		VuoInputData(VuoReal, {"default":1.0}) amplitude,
		VuoInputData(VuoReal, {"default":0.0}) offset,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = sin(input / period + phase) * amplitude + offset;
}
