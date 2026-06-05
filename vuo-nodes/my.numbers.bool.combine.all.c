/**
 * @file
 * my.numbers.bool.combine.all node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_All
 * - Category: Operators/Lib/numbers/bool/combine
 * - Source: external/tixl/Operators/Lib/numbers/bool/combine/All.cs
 * - Default: Input=false from All.t3
 * - Primary output: bool Result (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL MultiInputSlot<bool> is carried as a VuoList_VuoBoolean input.
 */

#include "VuoList_VuoBoolean.h"

VuoModuleMetadata({
					 "title" : "my_All",
					 "description" : "Returns true only when all connected booleans are true; empty input returns false like TiXL.",
					 "keywords" : [ "tixl", "numbers", "bool", "all", "and", "aggregate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoBoolean) inputValues,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	unsigned long count = inputValues ? VuoListGetCount_VuoBoolean(inputValues) : 0;
	if (count == 0)
	{
		*result = false;
		return;
	}

	bool value = true;
	for (unsigned long i = 1; i <= count; ++i)
		value = value && VuoListGetValue_VuoBoolean(inputValues, i);
	*result = value;
}
