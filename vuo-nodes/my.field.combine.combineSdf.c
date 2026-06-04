/**
 * @file
 * my.field.combine.combineSdf node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/field/combine/CombineSDF.
 * TiXL source: external/tixl/Operators/Lib/field/combine/CombineSDF.cs
 */

#include "VuoText.h"

VuoModuleMetadata({
					 "title" : "my_CombineSDF",
					 "description" : "TiXL CombineSDF field contract adapter. Category: Operators/Lib/field/combine. Primary output: ShaderGraphNode. ColorForShaderGraph #D142B3. This is a bounded two-field Vuo adapter for TiXL's MultiInputSlot.",
					 "keywords" : [ "tixl", "field", "sdf", "combine", "shadergraphnode", "two-field" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger clampCombineMethod(VuoInteger combineMethod)
{
	if (combineMethod < 0)
		return 0;

	if (combineMethod > 12)
		return 12;

	return combineMethod;
}

void nodeEvent
(
		VuoInputEvent() update,
		VuoInputData(VuoText, {"name":"FieldA","default":""}) fieldA,
		VuoInputData(VuoText, {"name":"FieldB","default":""}) fieldB,
		VuoInputData(VuoInteger, {"default":2,"suggestedMin":0,"suggestedMax":12,"suggestedStep":1}) combineMethod,
		VuoInputData(VuoReal, {"default":0.0,"suggestedMin":0.0,"suggestedMax":2.0,"suggestedStep":0.01}) k,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	VuoInteger safeCombineMethod = clampCombineMethod(combineMethod);
	VuoReal safeK = k < 0.0 ? 0.0 : k;
	const char *safeFieldA = VuoText_isPopulated(fieldA) ? fieldA : "{\"tixlType\":\"ShaderGraphNode\",\"node\":\"EmptySDF\"}";
	const char *safeFieldB = VuoText_isPopulated(fieldB) ? fieldB : "{\"tixlType\":\"ShaderGraphNode\",\"node\":\"EmptySDF\"}";

	char *contract = VuoText_format(
			"{\"tixlType\":\"ShaderGraphNode\",\"node\":\"CombineSDF\",\"category\":\"Operators/Lib/field/combine\",\"adapter\":\"two-field-vuo-body\",\"combineMethod\":%lld,\"k\":%.8g,\"fieldA\":%s,\"fieldB\":%s}",
			(long long)safeCombineMethod,
			safeK,
			safeFieldA,
			safeFieldB);
	*result = VuoText_makeWithoutCopying(contract);
}
