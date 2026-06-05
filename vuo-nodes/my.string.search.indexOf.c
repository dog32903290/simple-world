/**
 * @file
 * my.string.search.indexOf node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/search/IndexOf.
 */

#include "VuoText.h"
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_IndexOf",
					 "description" : "TiXL IndexOf adapter. Source: external/tixl/Operators/Lib/string/search/IndexOf.cs. Category: Operators/Lib/string/search. Primary output: int (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "string", "search", "index", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) originalString,
		VuoInputData(VuoText, {"default":""}) searchPattern,
		VuoOutputData(VuoInteger, {"name":"Index"}) index
)
{
	if (!VuoText_isPopulated(originalString) || !VuoText_isPopulated(searchPattern))
	{
		*index = -1;
		return;
	}
	const char *found = strstr(originalString, searchPattern);
	*index = found ? (VuoInteger)(found - originalString) : -1;
}
