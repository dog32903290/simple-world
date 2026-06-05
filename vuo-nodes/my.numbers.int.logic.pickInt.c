/**
 * @file
 * my.numbers.int.logic.pickInt node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/logic/PickInt.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_PickInt",
					 "description" : "TiXL PickInt adapter. Source: external/tixl/Operators/Lib/numbers/int/logic/PickInt.cs. Category: Operators/Lib/numbers/int/logic. Primary output: int (ColorForValues #868C8D). Uses TiXL MathUtils.Mod index wrapping. Adapter-bounded for TiXL multi-input dirty-flag behavior; empty Vuo list emits 0.",
					 "keywords" : [ "tixl", "numbers", "int", "logic", "pick", "list", "index", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myPositiveMod(VuoInteger value, VuoInteger repeat)
{
	if (repeat == 0)
		return 0;
	VuoInteger result = value % repeat;
	return result < 0 ? repeat + result : result;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) inputValues,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoInteger, {"name":"Selected"}) selected
)
{
	unsigned long count = inputValues ? VuoListGetCount_VuoInteger(inputValues) : 0;
	if (count == 0)
	{
		*selected = 0;
		return;
	}

	VuoInteger wrappedIndex = myPositiveMod(index, (VuoInteger)count);
	*selected = VuoListGetValue_VuoInteger(inputValues, (unsigned long)wrappedIndex + 1);
}
