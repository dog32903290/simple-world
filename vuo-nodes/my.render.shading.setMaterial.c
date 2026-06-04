/**
 * @file
 * my.render.shading.setMaterial node implementation.
 *
 * Vuo body-layer contract adapter for TiXL Operators/Lib/render/shading/SetMaterial.
 */

#include "VuoColor.h"
#include "VuoText.h"

VuoModuleMetadata({
					 "title" : "my_SetMaterial",
					 "description" : "TiXL SetMaterial PBR contract adapter. Source: external/tixl/Operators/Lib/render/shading/SetMaterial.cs. Category: Operators/Lib/render/shading. Primary output: PbrMaterial contract text. ColorForCommands #22B8C2 / material context adapter.",
					 "keywords" : [ "tixl", "material", "pbr", "setmaterial", "shader", "context" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputEvent() update,
		VuoInputData(VuoText, {"default":"glass"}) MaterialId,
		VuoInputData(VuoColor, {"default":{"r":0.45,"g":0.8,"b":1.0,"a":1.0}}) BaseColor,
		VuoInputData(VuoColor, {"default":{"r":0.0,"g":0.0,"b":0.0,"a":1.0}}) EmissiveColor,
		VuoInputData(VuoReal, {"default":0.25,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) Roughness,
		VuoInputData(VuoReal, {"default":1.0,"suggestedMin":0.0,"suggestedMax":10.0,"suggestedStep":0.1}) Specular,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":0.0,"suggestedMax":1.0,"suggestedStep":0.01}) Metal,
		VuoOutputData(VuoText, {"name":"Reference"}) Reference
)
{
	const char *safeMaterialId = VuoText_isPopulated(MaterialId) ? MaterialId : "";
	VuoReal safeRoughness = Roughness < 0.0 ? 0.0 : (Roughness > 1.0 ? 1.0 : Roughness);
	VuoReal safeSpecular = Specular < 0.0 ? 0.0 : Specular;
	VuoReal safeMetal = Metal < 0.0 ? 0.0 : (Metal > 1.0 ? 1.0 : Metal);

	char *contract = VuoText_format(
			"{\"tixlType\":\"PbrMaterial\",\"node\":\"SetMaterial\",\"category\":\"Operators/Lib/render/shading\",\"materialId\":\"%s\",\"parameters\":{\"baseColor\":{\"r\":%.8g,\"g\":%.8g,\"b\":%.8g,\"a\":%.8g},\"emissiveColor\":{\"r\":%.8g,\"g\":%.8g,\"b\":%.8g,\"a\":%.8g},\"roughness\":%.8g,\"specular\":%.8g,\"metal\":%.8g},\"resources\":{\"baseColorMap\":\"DefaultAlbedoColorSrv\",\"emissiveColorMap\":\"DefaultEmissiveColorSrv\",\"roughnessMetallicOcclusionMap\":\"DefaultRoughnessMetallicOcclusionSrv\",\"normalMap\":\"DefaultNormalSrv\"},\"adapter\":\"vuo-body-contract\"}",
			safeMaterialId,
			BaseColor.r,
			BaseColor.g,
			BaseColor.b,
			BaseColor.a,
			EmissiveColor.r,
			EmissiveColor.g,
			EmissiveColor.b,
			EmissiveColor.a,
			safeRoughness,
			safeSpecular,
			safeMetal);
	*Reference = VuoText_makeWithoutCopying(contract);
}
