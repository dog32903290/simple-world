/**
 * @file
 * my.numbers.int.process.minInt node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/process/MinInt.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_MinInt",
					 "description" : "TiXL MinInt adapter. Source: external/tixl/Operators/Lib/numbers/int/process/MinInt.cs. Category: Operators/Lib/numbers/int/process. Primary output: int (ColorForValues #868C8D). Empty Vuo list emits TiXL Int32.MaxValue sentinel.",
					 "keywords" : [ "tixl", "numbers", "int", "process", "min", "list", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) ints,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	VuoInteger min = 2147483647;
	unsigned long count = ints ? VuoListGetCount_VuoInteger(ints) : 0;
	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoInteger value = VuoListGetValue_VuoInteger(ints, i);
		if (value < min)
			min = value;
	}
	*result = min;
}
