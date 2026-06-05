/**
 * @file
 * my.numbers.int.logic.isIntEven node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/logic/IsIntEven.
 */

VuoModuleMetadata({
					 "title" : "my_IsIntEven",
					 "description" : "TiXL IsIntEven adapter. Source: external/tixl/Operators/Lib/numbers/int/logic/IsIntEven.cs. Category: Operators/Lib/numbers/int/logic. Primary output: bool (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "numbers", "int", "logic", "even", "odd", "bool", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	*result = value % 2 == 0;
}
