/**
 * @file
 * my.numbers.int.process.maxInt node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/process/MaxInt.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_MaxInt",
					 "description" : "TiXL MaxInt adapter. Source: external/tixl/Operators/Lib/numbers/int/process/MaxInt.cs. Category: Operators/Lib/numbers/int/process. Primary output: int (ColorForValues #868C8D). Empty Vuo list emits TiXL Int32.MinValue sentinel.",
					 "keywords" : [ "tixl", "numbers", "int", "process", "max", "list", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) ints,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	VuoInteger max = -2147483647 - 1;
	unsigned long count = ints ? VuoListGetCount_VuoInteger(ints) : 0;
	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoInteger value = VuoListGetValue_VuoInteger(ints, i);
		if (value > max)
			max = value;
	}
	*result = max;
}
