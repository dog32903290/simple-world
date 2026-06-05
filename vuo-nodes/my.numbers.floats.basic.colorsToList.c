/**
 * @file
 * my.numbers.floats.basic.colorsToList node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ColorsToList
 * - Category: Operators/Lib/numbers/floats/basic
 * - Source: external/tixl/Operators/Lib/numbers/floats/basic/ColorsToList.cs
 * - Default: Colors=(1.0,1.0,1.0,1.0) from ColorsToList.t3
 * - Primary output: List<Vector4> Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 color inputs stand in for TiXL
 * MultiInputSlot<Vector4>. inputCount declares how many leading ports are
 * treated as connected.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"

VuoModuleMetadata({
					 "title" : "my_ColorsToList",
					 "description" : "Collects connected color inputs into a color list in TiXL input order.",
					 "keywords" : [ "tixl", "numbers", "floats", "color", "list", "multi-input", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color1,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color2,
		VuoInputData(VuoColor, {"default":{"r":1.0,"g":1.0,"b":1.0,"a":1.0}}) color3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoOutputData(VuoList_VuoColor, {"name":"Result"}) result
)
{
	VuoInteger connectedCount = inputCount;
	if (connectedCount < 0)
		connectedCount = 0;
	if (connectedCount > 3)
		connectedCount = 3;

	VuoList_VuoColor output = VuoListCreate_VuoColor();
	if (connectedCount >= 1)
		VuoListAppendValue_VuoColor(output, color1);
	if (connectedCount >= 2)
		VuoListAppendValue_VuoColor(output, color2);
	if (connectedCount >= 3)
		VuoListAppendValue_VuoColor(output, color3);

	*result = output;
}
