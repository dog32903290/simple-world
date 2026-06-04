/**
 * @file
 * my.numbers.float.adjust.ceil node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/Ceil.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Ceil",
					 "description" : "Exact TiXL Ceil scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Ceil.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "ceil", "ceiling", "round", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoOutputData(VuoReal) result
)
{
	*result = (VuoReal)ceil(value);
}
