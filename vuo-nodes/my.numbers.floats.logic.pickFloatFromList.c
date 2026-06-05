/**
 * @file
 * my.numbers.floats.logic.pickFloatFromList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickFloatFromList
 * - Category: Operators/Lib/numbers/floats/logic
 * - Source: external/tixl/Operators/Lib/numbers/floats/logic/PickFloatFromList.cs
 * - Default: Input=[5.0,17.0], Index=0 from PickFloatFromList.t3
 * - Primary output: float Selected (ColorForValues #868C8D)
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_PickFloatFromList",
					 "description" : "Picks a float from a list using TiXL positive modulo index wrapping.",
					 "keywords" : [ "tixl", "floats", "list", "pick", "index", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveMod(VuoInteger value, VuoInteger repeat)
{
	if (repeat == 0)
		return 0;
	VuoInteger result = value % repeat;
	return result < 0 ? result + repeat : result;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) input,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoReal, {"name":"Selected"}) selected
)
{
	unsigned long count = input ? VuoListGetCount_VuoReal(input) : 0;
	if (count == 0)
	{
		*selected = 0.0;
		return;
	}

	VuoInteger wrappedIndex = positiveMod(index, (VuoInteger)count);
	*selected = VuoListGetValue_VuoReal(input, (unsigned long)wrappedIndex + 1);
}
