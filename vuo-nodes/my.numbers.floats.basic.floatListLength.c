/**
 * @file
 * my.numbers.floats.basic.floatListLength node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_FloatListLength
 * - Category: Operators/Lib/numbers/floats/basic
 * - Source: external/tixl/Operators/Lib/numbers/floats/basic/FloatListLength.cs
 * - Default: Input=[] from FloatListLength.t3
 * - Primary output: int Length (ColorForValues #868C8D)
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_FloatListLength",
					 "description" : "Returns the number of elements in a float list.",
					 "keywords" : [ "tixl", "floats", "list", "length", "count", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) input,
		VuoOutputData(VuoInteger, {"name":"Length"}) length
)
{
	*length = input ? VuoListGetCount_VuoReal(input) : 0;
}
