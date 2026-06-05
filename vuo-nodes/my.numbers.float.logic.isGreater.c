/**
 * @file
 * my.numbers.float.logic.isGreater node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/logic/IsGreater.
 */

VuoModuleMetadata({
					 "title" : "my_IsGreater",
					 "description" : "TiXL IsGreater scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/logic/IsGreater.cs. Category: Operators/Lib/numbers/float/logic. Primary output: bool (ColorForValues #868C8D). Uses TiXL's strict Value > Threshold comparison.",
					 "keywords" : [ "tixl", "numbers", "float", "logic", "greater", "threshold", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoReal, {"default":1.0}) value,
		VuoInputData(VuoReal, {"default":0.5}) threshold,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	*result = value > threshold;
}
