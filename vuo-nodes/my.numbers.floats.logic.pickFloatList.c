/**
 * @file
 * my.numbers.floats.logic.pickFloatList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickFloatList
 * - Category: Operators/Lib/numbers/floats/logic
 * - Source: external/tixl/Operators/Lib/numbers/floats/logic/PickFloatList.cs
 * - Default: Input=[], Index=0 from PickFloatList.t3
 * - Primary output: List<float> Selected (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 input lists stand in for TiXL MultiInputSlot<List<float>>.
 * inputCount declares how many leading list ports are treated as connected.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_PickFloatList",
					 "description" : "Picks one float list from up to three connected lists using TiXL positive modulo index wrapping.",
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
		VuoInputData(VuoList_VuoReal) inputList1,
		VuoInputData(VuoList_VuoReal) inputList2,
		VuoInputData(VuoList_VuoReal) inputList3,
		VuoInputData(VuoInteger, {"default":3, "suggestedMin":0, "suggestedMax":3}) inputCount,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoInputData(VuoList_VuoReal) previousResult,
		VuoOutputData(VuoList_VuoReal, {"name":"Selected"}) selected
)
{
	if (inputCount <= 0)
	{
		*selected = previousResult ? VuoListCopy_VuoReal(previousResult) : VuoListCreate_VuoReal();
		return;
	}
	VuoInteger connectedCount = inputCount > 3 ? 3 : inputCount;

	VuoList_VuoReal inputs[3] = { inputList1, inputList2, inputList3 };
	VuoInteger wrappedIndex = positiveMod(index, connectedCount);
	VuoList_VuoReal chosen = inputs[wrappedIndex];
	*selected = chosen ? VuoListCopy_VuoReal(chosen) : VuoListCreate_VuoReal();
}
