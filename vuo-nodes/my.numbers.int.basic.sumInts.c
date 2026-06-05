/**
 * @file
 * my.numbers.int.basic.sumInts node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/SumInts.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_SumInts",
					 "description" : "TiXL SumInts adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/SumInts.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D). Empty Vuo list emits the multi-input default value, matching TiXL InputValues.GetValue(context).",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "sum", "list", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) inputValues,
		VuoInputData(VuoInteger, {"default":0}) defaultValue,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	VuoInteger total = 0;
	unsigned long count = inputValues ? VuoListGetCount_VuoInteger(inputValues) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		total += VuoListGetValue_VuoInteger(inputValues, i);

	*result = count == 0 ? defaultValue : total;
}
