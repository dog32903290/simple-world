/**
 * @file
 * my.numbers.float.basic.log node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Log.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Log",
					 "description" : "TiXL Log scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Log.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D). This is adapter-bounded only for visual proof of NaN cases; the node itself follows C# Math.Log(value, base), including NaN for base 1 or invalid domains.",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "log", "logarithm", "adapter-bounded", "NaN", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) value,
		VuoInputData(VuoReal, {"default":1.0}) base,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	if (value == 1.0 && base != 1.0)
	{
		*result = 0.0;
		return;
	}
	if (base == 1.0 || base <= 0.0 || value < 0.0)
	{
		*result = NAN;
		return;
	}
	*result = log(value) / log(base);
}
