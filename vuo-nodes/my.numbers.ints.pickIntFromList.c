/**
 * @file
 * my.numbers.ints.pickIntFromList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickIntFromList
 * - Category: Operators/Lib/numbers/ints
 * - Source: external/tixl/Operators/Lib/numbers/ints/PickIntFromList.cs
 * - Defaults: Input=[], Index=0 from PickIntFromList.t3
 * - Primary output: int Selected (ColorForValues #868C8D)
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_PickIntFromList",
					 "description" : "Picks an integer from a list using TiXL positive modulo index wrapping.",
					 "keywords" : [ "tixl", "ints", "list", "pick", "index" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveMod(VuoInteger value, VuoInteger repeat)
{
	VuoInteger result = value % repeat;
	return result < 0 ? result + repeat : result;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) input,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoInteger, {"name":"Selected"}) selected
)
{
	unsigned long count = input ? VuoListGetCount_VuoInteger(input) : 0;
	if (count == 0)
	{
		*selected = 0;
		return;
	}

	VuoInteger wrappedIndex = positiveMod(index, (VuoInteger)count);
	*selected = VuoListGetValue_VuoInteger(input, (unsigned long)wrappedIndex + 1);
}
