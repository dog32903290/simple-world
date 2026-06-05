/**
 * @file
 * my.numbers.float.trigonometry.cos node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/trigonometry/Cos.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Cos",
					 "description" : "TiXL Cos scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/trigonometry/Cos.cs. Category: Operators/Lib/numbers/float/trigonometry. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "trigonometry", "cos", "radians", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) input,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = cos(input);
}
