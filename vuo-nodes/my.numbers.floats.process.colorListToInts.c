/**
 * @file
 * my.numbers.floats.process.colorListToInts node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ColorListToInts
 * - Category: Operators/Lib/numbers/floats/process
 * - Source: external/tixl/Operators/Lib/numbers/floats/process/ColorListToInts.cs
 * - Default: ColorLists=[], OutputMode=0 from ColorListToInts.t3
 * - Primary output: List<int> Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 color-list inputs stand in for TiXL
 * MultiInputSlot<List<Vector4>>. inputCount declares how many leading ports
 * are treated as connected.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoInteger.h"

VuoModuleMetadata({
					 "title" : "my_ColorListToInts",
					 "description" : "Converts color list channels into clamped 0-255 integer lists using TiXL output modes.",
					 "keywords" : [ "tixl", "numbers", "floats", "color", "list", "int", "rgba", "argb", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoReal clampReal(VuoReal value, VuoReal min, VuoReal max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static void appendAsInt(VuoList_VuoInteger output, VuoReal value)
{
	VuoListAppendValue_VuoInteger(output, (VuoInteger)clampReal(value * 255.0, 0.0, 255.0));
}

static void appendChannelValues(VuoList_VuoInteger output, VuoColor color, VuoInteger outputMode)
{
	if (outputMode == 0)
	{
		appendAsInt(output, color.r);
		appendAsInt(output, color.g);
		appendAsInt(output, color.b);
		appendAsInt(output, color.a);
	}
	else if (outputMode == 1)
	{
		appendAsInt(output, color.a);
		appendAsInt(output, color.r);
		appendAsInt(output, color.g);
		appendAsInt(output, color.b);
	}
	else if (outputMode == 2)
	{
		appendAsInt(output, color.r);
		appendAsInt(output, color.g);
		appendAsInt(output, color.b);
	}
	else if (outputMode == 3)
	{
		appendAsInt(output, color.r);
	}
	else if (outputMode == 4)
	{
		appendAsInt(output, color.a);
	}
}

static void appendColorList(VuoList_VuoInteger output, VuoList_VuoColor colorList, VuoInteger outputMode)
{
	unsigned long count = colorList ? VuoListGetCount_VuoColor(colorList) : 0;
	for (unsigned long i = 1; i <= count; ++i)
		appendChannelValues(output, VuoListGetValue_VuoColor(colorList, i), outputMode);
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) colorList1,
		VuoInputData(VuoList_VuoColor) colorList2,
		VuoInputData(VuoList_VuoColor) colorList3,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) outputMode,
		VuoOutputData(VuoList_VuoInteger, {"name":"Result"}) result
)
{
	VuoInteger connectedCount = inputCount;
	if (connectedCount < 0)
		connectedCount = 0;
	if (connectedCount > 3)
		connectedCount = 3;

	VuoInteger mode = outputMode;
	if (mode < 0)
		mode = 0;
	if (mode > 4)
		mode = 4;

	VuoList_VuoInteger output = VuoListCreate_VuoInteger();
	if (connectedCount >= 1)
		appendColorList(output, colorList1, mode);
	if (connectedCount >= 2)
		appendColorList(output, colorList2, mode);
	if (connectedCount >= 3)
		appendColorList(output, colorList3, mode);

	*result = output;
}
