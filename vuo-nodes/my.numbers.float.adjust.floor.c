/**
 * @file
 * my.numbers.float.adjust.floor node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/Floor.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

VuoModuleMetadata({
					 "title" : "my_Floor",
					 "description" : "Exact TiXL Floor scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Floor.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "floor", "truncate", "round", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":29.0}) value,
		VuoOutputData(VuoReal) result
)
{
	*result = (VuoReal)(int)value;
}
