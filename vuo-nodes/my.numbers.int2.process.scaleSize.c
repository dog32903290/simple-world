/**
 * @file
 * my.numbers.int2.process.scaleSize node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_ScaleSize
 * - Category: Operators/Lib/numbers/int2/process
 * - Source: external/tixl/Operators/Lib/numbers/int2/process/ScaleSize.cs
 * - Defaults: InputSize=(0,0), Stretch=(1,1), Scale=1
 * - Primary output: Int2 (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_ScaleSize",
					 "description" : "Uniformly scales a TiXL Int2 size with per-axis stretch.",
					 "keywords" : [ "tixl", "int2", "resolution", "scale", "stretch" ],
					 "version" : "1.0.0",
				 });

static VuoInteger toInt(VuoReal value)
{
	return (VuoInteger)value;
}

void nodeEvent
(
		VuoInputData(VuoPoint2d, {"default":{"x":0.0,"y":0.0}}) inputSize,
		VuoInputData(VuoPoint2d, {"default":{"x":1.0,"y":1.0}}) stretch,
		VuoInputData(VuoReal, {"default":1.0}) scale,
		VuoOutputData(VuoPoint2d, {"name":"Result"}) result
)
{
	VuoInteger width = (VuoInteger)(toInt(inputSize.x) * scale * stretch.x);
	VuoInteger height = (VuoInteger)(toInt(inputSize.y) * scale * stretch.y);
	*result = (VuoPoint2d){width, height};
}
