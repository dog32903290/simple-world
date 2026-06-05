/**
 * @file
 * my.numbers.float.logic.compare node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/logic/Compare.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_Compare",
					 "description" : "TiXL Compare scalar adapter. Source: external/tixl/Operators/Lib/numbers/float/logic/Compare.cs. Category: Operators/Lib/numbers/float/logic. Primary output: bool (ColorForValues #868C8D). Modes: 0 smaller, 1 equal, 2 larger, 3 not equal.",
					 "keywords" : [ "tixl", "numbers", "float", "logic", "compare", "precision", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myClampMode(VuoInteger mode)
{
	if (mode < 0)
		return 0;
	if (mode > 3)
		return 3;
	return mode;
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoReal, {"default":0.0}) testValue,
		VuoInputData(VuoInteger, {"default":1, "suggestedMin":0, "suggestedMax":3}) mode,
		VuoInputData(VuoReal, {"default":0.001}) precision,
		VuoOutputData(VuoBoolean, {"name":"IsTrue"}) isTrue
)
{
	switch (myClampMode(mode))
	{
		case 0:
			*isTrue = value < testValue;
			break;
		case 1:
			*isTrue = fabs(value - testValue) < precision;
			break;
		case 2:
			*isTrue = value > testValue;
			break;
		case 3:
		default:
			*isTrue = fabs(value - testValue) >= precision;
			break;
	}
}
