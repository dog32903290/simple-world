/**
 * @file
 * my.numbers.color.pickGradient node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_PickGradient
 * - Category: Operators/Lib/numbers/color
 * - Source: external/tixl/Operators/Lib/numbers/color/PickGradient.cs
 * - Default: Gradients=[], Index=0 from PickGradient.t3
 * - Primary output: Gradient Selected (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: TiXL Gradient maps to color list + position list + interpolation enum.
 * Fixed 3 gradient inputs stand in for TiXL MultiInputSlot<Gradient>.
 */

#include "VuoColor.h"
#include "VuoList_VuoColor.h"
#include "VuoList_VuoReal.h"

VuoModuleMetadata({
					 "title" : "my_PickGradient",
					 "description" : "Picks one bounded TiXL-style gradient payload by positive modulo.",
					 "keywords" : [ "tixl", "numbers", "color", "gradient", "pick", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static VuoInteger positiveModulo(VuoInteger value, VuoInteger repeat)
{
	if (repeat == 0)
		return 0;
	VuoInteger x = value % repeat;
	return x < 0 ? repeat + x : x;
}

static VuoList_VuoColor safeColorList(VuoList_VuoColor list)
{
	return list ? VuoListCopy_VuoColor(list) : VuoListCreate_VuoColor();
}

static VuoList_VuoReal safeRealList(VuoList_VuoReal list)
{
	return list ? VuoListCopy_VuoReal(list) : VuoListCreate_VuoReal();
}

void nodeEvent
(
		VuoInputData(VuoList_VuoColor) gradient1Colors,
		VuoInputData(VuoList_VuoReal) gradient1Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient1Interpolation,
		VuoInputData(VuoList_VuoColor) gradient2Colors,
		VuoInputData(VuoList_VuoReal) gradient2Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient2Interpolation,
		VuoInputData(VuoList_VuoColor) gradient3Colors,
		VuoInputData(VuoList_VuoReal) gradient3Positions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) gradient3Interpolation,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":3,"suggestedStep":1}) inputCount,
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoInputData(VuoList_VuoColor) previousSelectedColors,
		VuoInputData(VuoList_VuoReal) previousSelectedPositions,
		VuoInputData(VuoInteger, {"default":0,"suggestedMin":0,"suggestedMax":4,"suggestedStep":1}) previousSelectedInterpolation,
		VuoOutputData(VuoList_VuoColor, {"name":"Selected Colors"}) selectedColors,
		VuoOutputData(VuoList_VuoReal, {"name":"Selected Positions"}) selectedPositions,
		VuoOutputData(VuoInteger, {"name":"Selected Interpolation"}) selectedInterpolation
)
{
	VuoInteger connectedCount = inputCount;
	if (connectedCount < 0)
		connectedCount = 0;
	if (connectedCount > 3)
		connectedCount = 3;

	if (connectedCount == 0)
	{
		// TiXL returns without changing Selected.Value; Vuo exposes that cache as previousSelected.
		*selectedColors = safeColorList(previousSelectedColors);
		*selectedPositions = safeRealList(previousSelectedPositions);
		*selectedInterpolation = previousSelectedInterpolation;
		return;
	}

	VuoInteger picked = positiveModulo(index, connectedCount);
	if (picked == 0)
	{
		*selectedColors = safeColorList(gradient1Colors);
		*selectedPositions = safeRealList(gradient1Positions);
		*selectedInterpolation = gradient1Interpolation;
	}
	else if (picked == 1)
	{
		*selectedColors = safeColorList(gradient2Colors);
		*selectedPositions = safeRealList(gradient2Positions);
		*selectedInterpolation = gradient2Interpolation;
	}
	else
	{
		*selectedColors = safeColorList(gradient3Colors);
		*selectedPositions = safeRealList(gradient3Positions);
		*selectedInterpolation = gradient3Interpolation;
	}
}
