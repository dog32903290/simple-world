/**
 * @file
 * my.field.generate.sdf.boxSdf node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/field/generate/sdf/BoxSDF.
 * TiXL source: external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.cs
 */

#include "VuoText.h"
#include "VuoPoint3d.h"

VuoModuleMetadata({
					 "title" : "my_BoxSDF",
					 "description" : "TiXL BoxSDF field contract adapter. Category: Operators/Lib/field/generate/sdf. Primary output: ShaderGraphNode. ColorForShaderGraph #D142B3.",
					 "keywords" : [ "tixl", "field", "sdf", "box", "rounded", "shadergraphnode" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputEvent() update,
		VuoInputData(VuoPoint3d, {"default":{"x":0.0,"y":0.0,"z":0.0}}) center,
		VuoInputData(VuoPoint3d, {"default":{"x":1.0,"y":1.0,"z":1.0}}) size,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.0,"suggestedMax":4.0,"suggestedStep":0.01}) uniformScale,
		VuoInputData(VuoReal, {"default":0.05,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) edgeRadius,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	VuoReal safeUniformScale = uniformScale < 0.0 ? 0.0 : uniformScale;
	VuoReal safeEdgeRadius = edgeRadius < 0.0 ? 0.0 : edgeRadius;
	VuoPoint3d safeSize = VuoPoint3d_make(
			size.x < 0.0 ? -size.x : size.x,
			size.y < 0.0 ? -size.y : size.y,
			size.z < 0.0 ? -size.z : size.z);

	char *contract = VuoText_format(
			"{\"tixlType\":\"ShaderGraphNode\",\"node\":\"BoxSDF\",\"category\":\"Operators/Lib/field/generate/sdf\",\"center\":{\"x\":%.8g,\"y\":%.8g,\"z\":%.8g},\"size\":{\"x\":%.8g,\"y\":%.8g,\"z\":%.8g},\"uniformScale\":%.8g,\"edgeRadius\":%.8g}",
			center.x,
			center.y,
			center.z,
			safeSize.x,
			safeSize.y,
			safeSize.z,
			safeUniformScale,
			safeEdgeRadius);
	*result = VuoText_makeWithoutCopying(contract);
}
