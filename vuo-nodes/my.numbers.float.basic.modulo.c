/**
 * @file
 * my.numbers.float.basic.modulo node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/basic/Modulo.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Modulo",
					 "description" : "TiXL Modulo scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/basic/Modulo.cs. Category: Operators/Lib/numbers/float/basic. Primary output: float (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "float", "basic", "modulo" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) Value,
		VuoInputData(VuoReal, {"default":1.0}) ModuloValue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = ModuloValue != 0.0 ? Value - ModuloValue * floor(Value / ModuloValue) : 0.0;
}
