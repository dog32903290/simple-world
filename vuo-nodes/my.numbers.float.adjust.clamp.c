/**
 * @file
 * my.numbers.float.adjust.clamp node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/Clamp.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Clamp",
					 "description" : "Exact TiXL Clamp scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Clamp.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "clamp", "limit", "range", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoReal myClamp(VuoReal value, VuoReal min, VuoReal max)
{
	return fmin(fmax(value, min), max);
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoReal, {"default":0.0}) min,
		VuoInputData(VuoReal, {"default":1.0}) max,
		VuoOutputData(VuoReal) result
)
{
	*result = myClamp(value, min, max);
}
