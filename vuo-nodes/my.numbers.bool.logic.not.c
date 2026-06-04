/**
 * @file
 * my.numbers.bool.logic.not node implementation.
 *
 * Exact Vuo body-layer node for TiXL Operators/Lib/numbers/bool/logic/Not.
 */

VuoModuleMetadata({
					 "title" : "my_Not",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/logic/Not.cs. Category: Operators/Lib/numbers/bool/logic. Primary output: Result bool. Primary output type color: ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "bool", "not", "invert", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) boolValue,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	*result = !boolValue;
}
