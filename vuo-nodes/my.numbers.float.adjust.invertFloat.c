/**
 * @file
 * my.numbers.float.adjust.invertFloat node implementation.
 *
 * Exact TiXL port of external/tixl/Operators/Lib/numbers/float/adjust/InvertFloat.cs.
 * Category: Operators/Lib/numbers/float/adjust. Primary output: float (ColorForValues #868C8D).
 */

VuoModuleMetadata({
					 "title" : "my_InvertFloat",
					 "description" : "Exact TiXL InvertFloat scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/adjust/InvertFloat.cs. Category: Operators/Lib/numbers/float/adjust. Primary output: float, ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "float", "adjust", "invert", "negate", "sign", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) a,
		VuoInputData(VuoBoolean, {"default":true}) invert,
		VuoOutputData(VuoReal) result
)
{
	VuoReal sign = invert ? -1.0 : 1.0;
	*result = sign * a;
}
