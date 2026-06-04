/**
 * @file
 * my.numbers.bool.logic.xor node implementation.
 *
 * Exact Vuo body-layer node for TiXL Operators/Lib/numbers/bool/logic/Xor.
 */

VuoModuleMetadata({
					 "title" : "my_Xor",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/logic/Xor.cs. Category: Operators/Lib/numbers/bool/logic. Primary output: Result bool. Primary output type color: ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "bool", "xor", "exclusive", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) a,
		VuoInputData(VuoBoolean, {"default":true}) b,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	*result = b ? !a : a;
}
