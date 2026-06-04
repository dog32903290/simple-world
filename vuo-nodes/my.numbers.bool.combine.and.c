/**
 * @file
 * my.numbers.bool.combine.and node implementation.
 *
 * Exact Vuo body-layer node for TiXL Operators/Lib/numbers/bool/combine/And.
 */

VuoModuleMetadata({
					 "title" : "my_And",
					 "description" : "TiXL source: external/tixl/Operators/Lib/numbers/bool/combine/And.cs. Category: Operators/Lib/numbers/bool/combine. Primary output: Result bool. Primary output type color: ColorForValues #868C8D.",
					 "keywords" : [ "tixl", "numbers", "bool", "and", "value", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoBoolean, {"default":false}) a,
		VuoInputData(VuoBoolean, {"default":false}) b,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	*result = a & b;
}
