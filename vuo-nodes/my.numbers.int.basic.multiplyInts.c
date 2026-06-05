/**
 * @file
 * my.numbers.int.basic.multiplyInts node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/MultiplyInts.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_MultiplyInts",
					 "description" : "TiXL MultiplyInts adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/MultiplyInts.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D). Empty Vuo list emits 0, matching TiXL connectedCount==0 behavior.",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "multiply", "list", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) inputValues,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	VuoInteger total = 1;
	unsigned long count = inputValues ? VuoListGetCount_VuoInteger(inputValues) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		total *= VuoListGetValue_VuoInteger(inputValues, i);

	*result = count == 0 ? 0 : total;
}
