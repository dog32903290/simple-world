/**
 * @file
 * my.numbers.int2.process.makeResolution node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MakeResolution
 * - Category: Operators/Lib/numbers/int2/process
 * - Source: external/tixl/Operators/Lib/numbers/int2/process/MakeResolution.cs
 * - Defaults: Width=0, Height=0 from MakeResolution.t3
 * - Primary output: Int2 (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_MakeResolution",
					 "description" : "Creates a TiXL Int2 resolution from width and height.",
					 "keywords" : [ "tixl", "int2", "resolution", "make", "size" ],
					 "version" : "1.0.0",
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) width,
		VuoInputData(VuoInteger, {"default":0}) height,
		VuoOutputData(VuoPoint2d, {"name":"Size"}) size
)
{
	*size = (VuoPoint2d){width, height};
}
