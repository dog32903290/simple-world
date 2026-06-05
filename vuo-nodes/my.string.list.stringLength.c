/**
 * @file
 * my.string.list.stringLength node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/list/StringLength.
 */

#include "VuoText.h"
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_StringLength",
					 "description" : "TiXL StringLength adapter. Source: external/tixl/Operators/Lib/string/list/StringLength.cs. Category: Operators/Lib/string/list. Primary output: int (ColorForValues #868C8D). This is adapter-bounded for non-ASCII UTF-16 code-unit parity; Vuo text is UTF-8 and this adapter uses byte length.",
					 "keywords" : [ "tixl", "string", "length", "adapter-bounded", "UTF-16", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoText, {"default":"ten plus eleven is 21"}) inputString,
		VuoOutputData(VuoInteger, {"name":"Length"}) length
)
{
	*length = inputString ? (VuoInteger)strlen(inputString) : 0;
}
