/**
 * @file
 * my.numbers.floats.process.combineFloatLists node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_CombineFloatLists
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/CombineFloatLists.cs
 * - Default: InputLists=[] from CombineFloatLists.t3
 * - Primary output: List<float> Selected (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 input lists stand in for TiXL
 * MultiInputSlot<List<float>>. inputCount declares how many leading ports are
 * treated as connected.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_CombineFloatLists",
					 "description" : "Concatenates connected non-empty float lists in TiXL input order.",
					 "keywords" : [ "tixl", "numbers", "floats", "list", "process", "combine", "concat", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static void appendList(VuoList_VuoReal output, VuoList_VuoReal input)
{
	unsigned long count = input ? VuoListGetCount_VuoReal(input) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		VuoListAppendValue_VuoReal(output, VuoListGetValue_VuoReal(input, i));
}

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) inputList1,
		VuoInputData(VuoList_VuoReal) inputList2,
		VuoInputData(VuoList_VuoReal) inputList3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoOutputData(VuoList_VuoReal, {"name":"Selected"}) selected
)
{
	VuoInteger connectedCount = inputCount;
	if (connectedCount < 0)
		connectedCount = 0;
	if (connectedCount > 3)
		connectedCount = 3;

	VuoList_VuoReal output = VuoListCreate_VuoReal();
	if (connectedCount >= 1)
		appendList(output, inputList1);
	if (connectedCount >= 2)
		appendList(output, inputList2);
	if (connectedCount >= 3)
		appendList(output, inputList3);

	*selected = output;
}
