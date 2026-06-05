/**
 * @file
 * my.numbers.floats.basic.floatsToList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_FloatsToList
 * - Category: Operators/Lib/numbers/floats/basic
 * - Source: external/tixl/Operators/Lib/numbers/floats/basic/FloatsToList.cs
 * - Default: Input=0.0 from FloatsToList.t3
 * - Primary output: List<float> Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: Vuo receives the collected TiXL multi-input floats as one real list port.
 */

#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_FloatsToList",
					 "description" : "Converts collected float inputs into a float list.",
					 "keywords" : [ "tixl", "floats", "list", "collect", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoList_VuoReal) inputValues,
		VuoOutputData(VuoList_VuoReal, {"name":"Result"}) result
)
{
	*result = inputValues ? VuoListCopy_VuoReal(inputValues) : VuoListCreate_VuoReal();
}
