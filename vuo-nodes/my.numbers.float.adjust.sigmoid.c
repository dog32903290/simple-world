/**
 * @file
 * my.numbers.float.adjust.sigmoid node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/adjust/Sigmoid.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Sigmoid",
					 "description" : "TiXL Sigmoid scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Sigmoid.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D). Note TiXL uses 1/(1+e^(Stretch*Value)), so positive values lower the result.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "sigmoid", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) value,
		VuoInputData(VuoReal, {"default":1.0}) stretch,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = 1.0 / (1.0 + pow(M_E, stretch * value));
}
