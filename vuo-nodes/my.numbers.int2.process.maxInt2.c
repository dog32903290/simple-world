/**
 * @file
 * my.numbers.int2.process.maxInt2 node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MaxInt2
 * - Category: Operators/Lib/numbers/int2/process
 * - Source: external/tixl/Operators/Lib/numbers/int2/process/MaxInt2.cs
 * - Defaults: Sizes=(0,0) from MaxInt2.t3
 * - Primary output: Int2 (ColorForValues #868C8D)
 *
 * Vuo body-layer mapping:
 * TiXL Int2 is carried as VuoPoint2d for Vuo wiring, with x=Width and y=Height.
 */

#include "VuoList_VuoPoint2d.h"
#include "VuoPoint2d.h"

VuoModuleMetadata({
					 "title" : "my_MaxInt2",
					 "description" : "Returns the component-wise maximum width and height from a list of TiXL Int2 sizes.",
					 "keywords" : [ "tixl", "int2", "resolution", "max", "size" ],
					 "version" : "1.0.0",
				 });

static VuoInteger toInt(VuoReal value)
{
	return (VuoInteger)value;
}

void nodeEvent
(
		VuoInputData(VuoList_VuoPoint2d) sizes,
		VuoOutputData(VuoPoint2d, {"name":"MaxSize"}) maxSize
)
{
	VuoInteger maxWidth = 0;
	VuoInteger maxHeight = 0;
	unsigned long count = sizes ? VuoListGetCount_VuoPoint2d(sizes) : 0;
	for (unsigned long i = 1; i <= count; ++i)
	{
		VuoPoint2d size = VuoListGetValue_VuoPoint2d(sizes, i);
		VuoInteger width = toInt(size.x);
		VuoInteger height = toInt(size.y);
		maxWidth = width > maxWidth ? width : maxWidth;
		maxHeight = height > maxHeight ? height : maxHeight;
	}
	*maxSize = (VuoPoint2d){maxWidth, maxHeight};
}
