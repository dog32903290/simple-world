/**
 * @file
 * my.numbers.ints.intsToList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_IntsToList
 * - Category: Operators/Lib/numbers/ints
 * - Source: external/tixl/Operators/Lib/numbers/ints/IntsToList.cs
 * - Defaults: Input=0 from IntsToList.t3
 * - Primary output: List<int> Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: Vuo receives the collected TiXL multi-input integers as one integer list port.
 */

#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_IntsToList",
					 "description" : "Converts collected integer inputs into an integer list.",
					 "keywords" : [ "tixl", "ints", "list", "collect" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoInteger) inputValues,
		VuoOutputData(VuoList_VuoInteger, {"name":"Result"}) result
)
{
	*result = inputValues ? VuoListCopy_VuoInteger(inputValues) : VuoListCreate_VuoInteger();
}
