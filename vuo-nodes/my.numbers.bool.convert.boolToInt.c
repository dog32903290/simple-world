/**
 * @file
 * my.numbers.bool.convert.boolToInt node implementation.
 *
 * Exact Vuo body-layer node for TiXL Operators/Lib/numbers/bool/convert/BoolToInt.
 */

VuoModuleMetadata({
					 "title" : "my_BoolToInt",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/convert/BoolToInt.cs. Category: Operators/Lib/numbers/bool/convert. Primary output: Result int. Primary output type color: ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "bool", "int", "convert", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) boolValue,
		VuoInputData(VuoInteger, {"default":0}) resultForFalse,
		VuoInputData(VuoInteger, {"default":1}) resultForTrue,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = boolValue ? resultForTrue : resultForFalse;
}
