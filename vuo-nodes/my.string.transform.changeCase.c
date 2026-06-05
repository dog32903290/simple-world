/**
 * @file
 * my.string.transform.changeCase node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/transform/ChangeCase.
 */

#include "VuoText.h"
#include "VuoTextCase.h"

VuoModuleMetadata({
					 "title" : "my_ChangeCase",
					 "description" : "TiXL ChangeCase adapter. Source: external/tixl/Operators/Lib/string/transform/ChangeCase.cs. Category: Operators/Lib/string/transform. Primary output: string (ColorForString #779552). Mode 0 maps to upper-case; mode 1 maps to lower-case.",
					 "keywords" : [ "tixl", "string", "transform", "case", "upper", "lower", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) inputText,
		VuoInputData(VuoInteger, {"default":0}) mode,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	*result = VuoText_changeCase(inputText ? inputText : "", mode == 1 ? VuoTextCase_LowercaseAll : VuoTextCase_UppercaseAll);
}
