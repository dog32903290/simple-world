/**
 * @file
 * my.numbers.ints.intListLength node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_IntListLength
 * - Category: Operators/Lib/numbers/ints
 * - Source: external/tixl/Operators/Lib/numbers/ints/IntListLength.cs
 * - Defaults: Input=[] from IntListLength.t3
 * - Primary output: int Length (ColorForValues #868C8D)
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_IntListLength",
					 "description" : "Returns the number of elements in an integer list.",
					 "keywords" : [ "tixl", "ints", "list", "length", "count" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) input,
		VuoOutputData(VuoInteger, {"name":"Length"}) length
)
{
	*length = input ? (VuoInteger)VuoListGetCount_VuoInteger(input) : 0;
}
