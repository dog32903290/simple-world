/**
 * @file
 * my.numbers.float.adjust.abs node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/Abs.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

VuoModuleMetadata({
					 "title" : "my_Abs",
					 "description" : "Exact TiXL Abs scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/Abs.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "absolute", "abs", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoOutputData(VuoReal) result
)
{
	*result = value > 0.0 ? value : (-1.0 * value);
}
