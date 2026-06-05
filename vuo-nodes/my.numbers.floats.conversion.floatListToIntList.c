/**
 * @file
 * my.numbers.floats.conversion.floatListToIntList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_FloatListToIntList
 * - Category: Operators/Lib/numbers/floats/conversion
 * - Source: external/tixl/Operators/Lib/numbers/floats/conversion/FloatListToIntList.cs
 * - Default: FloatList=[] from FloatListToIntList.t3
 * - Primary output: List<int> Result (ColorForValues #868C8D)
 */

#include "VuoList_VuoInteger.h"
#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_FloatListToIntList",
					 "description" : "Converts each float in a list to an integer by truncating toward zero.",
					 "keywords" : [ "tixl", "floats", "ints", "list", "convert", "truncate", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) floatList,
		VuoOutputData(VuoList_VuoInteger, {"name":"Result"}) result
)
{
	VuoList_VuoInteger output = VuoListCreate_VuoInteger();
	unsigned long count = floatList ? VuoListGetCount_VuoReal(floatList) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		VuoListAppendValue_VuoInteger(output, (VuoInteger)VuoListGetValue_VuoReal(floatList, i));
	*result = output;
}
