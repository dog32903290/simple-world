/**
 * @file
 * my.numbers.floats.conversion.intListToFloatList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_IntListToFloatList
 * - Category: Operators/Lib/numbers/floats/conversion
 * - Source: external/tixl/Operators/Lib/numbers/floats/conversion/IntListToFloatList.cs
 * - Default: IntList=[] from IntListToFloatList.t3
 * - Primary output: List<float> Result (ColorForValues #868C8D)
 */

#include "VuoList_VuoInteger.h"
#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_IntListToFloatList",
					 "description" : "Converts each integer in a list to a float.",
					 "keywords" : [ "tixl", "floats", "ints", "list", "convert", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) intList,
		VuoOutputData(VuoList_VuoReal, {"name":"Result"}) result
)
{
	VuoList_VuoReal output = VuoListCreate_VuoReal();
	unsigned long count = intList ? VuoListGetCount_VuoInteger(intList) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		VuoListAppendValue_VuoReal(output, (VuoReal)VuoListGetValue_VuoInteger(intList, i));
	*result = output;
}
