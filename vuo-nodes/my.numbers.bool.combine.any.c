/**
 * @file
 * my.numbers.bool.combine.any node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_Any
 * - Category: Operators/Lib/numbers/bool/combine
 * - Source: external/tixl/Operators/Lib/numbers/bool/combine/Any.cs
 * - Default: Input=false from Any.t3
 * - Primary output: bool Result (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL MultiInputSlot<bool> is carried as a VuoList_VuoBoolean input.
 */

#include "VuoList_VuoBoolean.h"

VuoModuleMetadata({
					 "title" : "my_Any",
					 "description" : "Returns true when any connected boolean is true; empty input returns false like TiXL.",
					 "keywords" : [ "tixl", "numbers", "bool", "any", "or", "aggregate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoBoolean) inputValues,
		VuoOutputData(VuoBoolean, {"name":"Result"}) result
)
{
	unsigned long count = inputValues ? VuoListGetCount_VuoBoolean(inputValues) : 0;
	bool value = false;
	for (unsigned long i = 1; i <= count; ++i)
		if (VuoListGetValue_VuoBoolean(inputValues, i))
		{
			value = true;
			break;
		}
	*result = value;
}
