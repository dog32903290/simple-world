/**
 * @file
 * my.numbers.color.combineColorLists node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_CombineColorLists
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/CombineColorLists.cs
 * - Default: InputLists=[] from CombineColorLists.t3
 * - Primary output: List<Vector4> Selected (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 color-list inputs stand in for TiXL
 * MultiInputSlot<List<Vector4>>. inputCount declares how many leading ports
 * are treated as connected.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"

VuoModuleMetadata({
					 "title" : "my_CombineColorLists",
					 "description" : "Concatenates connected non-empty color lists in TiXL input order.",
					 "keywords" : [ "tixl", "numbers", "color", "list", "combine", "concat", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static void appendColorList(VuoList_VuoColor output, VuoList_VuoColor input)
{
	unsigned long count = input ? VuoListGetCount_VuoColor(input) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		VuoListAppendValue_VuoColor(output, VuoListGetValue_VuoColor(input, i));
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) inputList1,
		VuoInputData(VuoList_VuoColor) inputList2,
		VuoInputData(VuoList_VuoColor) inputList3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoOutputData(VuoList_VuoColor, {"name":"Selected"}) selected
)
{
	VuoInteger connectedCount = inputCount;
	if (connectedCount < 0)
		connectedCount = 0;
	if (connectedCount > 3)
		connectedCount = 3;

	VuoList_VuoColor output = VuoListCreate_VuoColor();
	if (connectedCount >= 1)
		appendColorList(output, inputList1);
	if (connectedCount >= 2)
		appendColorList(output, inputList2);
	if (connectedCount >= 3)
		appendColorList(output, inputList3);

	*selected = output;
}
