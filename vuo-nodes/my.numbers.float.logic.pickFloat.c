/**
 * @file
 * my.numbers.float.logic.pickFloat node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/logic/PickFloat.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_PickFloat",
					 "description" : "TiXL PickFloat adapter. Source: external/tixl/Operators/Lib/numbers/float/logic/PickFloat.cs. Category: Operators/Lib/numbers/float/logic. Primary output: float (ColorForValues #868C8D). Uses TiXL MathUtils.Mod index wrapping. Adapter-bounded for TiXL multi-input dirty-flag behavior; empty Vuo list emits 0.",
					 "keywords" : [ "tixl", "numbers", "float", "logic", "pick", "list", "index", "ColorForValues", "#868C8D" ],
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
		VuoInputData(VuoList_VuoReal) floatValues,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoReal, {"name":"Selected"}) selected
)
{
	unsigned long count = floatValues ? VuoListGetCount_VuoReal(floatValues) : 0;
	if (count == 0)
	{
		*selected = 0.0;
		return;
	}

	VuoInteger wrappedIndex = myPositiveMod(index, (VuoInteger)count);
	*selected = VuoListGetValue_VuoReal(floatValues, (unsigned long)wrappedIndex + 1);
}
