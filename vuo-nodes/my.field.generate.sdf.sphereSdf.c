/**
 * @file
 * my.field.generate.sdf.sphereSdf node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/field/generate/sdf/SphereSDF.
 */

#include "VuoText.h"
#include "VuoPoint3d.h"

VuoModuleMetadata({
					 "title" : "my_SphereSDF",
					 "description" : "TiXL SphereSDF field contract adapter. Category: Operators/Lib/field/generate/sdf. Primary output: ShaderGraphNode.",
					 "keywords" : [ "tixl", "field", "sdf", "sphere", "shadergraphnode" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputEvent() update,
		VuoInputData(VuoPoint3d, {"default":{"x":0.0,"y":0.0,"z":0.0}}) center,
		VuoInputData(VuoReal, {"default":0.5,"suggestedMin":0.0,"suggestedMax":2.0,"suggestedStep":0.01}) radius,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	VuoReal renderRadius = radius < 0.0 ? 0.0 : radius;
	char *contract = VuoText_format(
			"{\"tixlType\":\"ShaderGraphNode\",\"node\":\"SphereSDF\",\"category\":\"Operators/Lib/field/generate/sdf\",\"center\":{\"x\":%.8g,\"y\":%.8g,\"z\":%.8g},\"radius\":%.8g}",
			center.x,
			center.y,
			center.z,
			renderRadius);
	*result = VuoText_makeWithoutCopying(contract);
}
