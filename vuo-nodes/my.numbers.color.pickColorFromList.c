/**
 * @file
 * my.numbers.color.pickColorFromList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickColorFromList
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/PickColorFromList.cs
 * - Default: Input=[], Index=0 from PickColorFromList.t3
 * - Primary output: Vector4 Selected (ColorForValues #868C8D)
 *
 * TiXL returns early for null/empty input and keeps the previous Selected slot
 * value. Vuo exposes that cache behavior through previousSelected.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"

VuoModuleMetadata({
					 "title" : "my_PickColorFromList",
					 "description" : "Picks a color from a color list using TiXL positive modulo indexing.",
					 "keywords" : [ "tixl", "numbers", "color", "list", "pick", "modulo", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveMod(VuoInteger value, VuoInteger mod)
{
	if (mod == 0)
		return 0;
	VuoInteger result = value % mod;
	return result < 0 ? result + mod : result;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) input,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) previousSelected,
		VuoOutputData(VuoColor, {"name":"Selected"}) selected
)
{
	unsigned long count = input ? VuoListGetCount_VuoColor(input) : 0;
	if (count == 0)
	{
		*selected = previousSelected;
		return;
	}

	*selected = VuoListGetValue_VuoColor(input, (unsigned long)positiveMod(index, (VuoInteger)count) + 1);
}
