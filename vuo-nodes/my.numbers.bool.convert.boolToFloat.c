/**
 * @file
 * my.numbers.bool.convert.boolToFloat node implementation.
 *
 * Exact Vuo body-layer node for TiXL Operators/Lib/numbers/bool/convert/BoolToFloat.
 */

VuoModuleMetadata({
					 "title" : "my_BoolToFloat",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/convert/BoolToFloat.cs. Category: Operators/Lib/numbers/bool/convert. Primary output: Result float. Primary output type color: ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "bool", "float", "convert", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) boolValue,
		VuoInputData(VuoReal, {"default":0.0}) forFalse,
		VuoInputData(VuoReal, {"default":1.0}) forTrue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	*result = boolValue ? forTrue : forFalse;
}
