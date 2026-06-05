/**
 * @file
 * my.string.combine.stringRepeat node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/combine/StringRepeat.
 */

#include "VuoText.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_StringRepeat",
					 "description" : "TiXL StringRepeat adapter. Source: external/tixl/Operators/Lib/string/combine/StringRepeat.cs. Category: Operators/Lib/string/combine. Primary output: string (ColorForString #779552).",
					 "keywords" : [ "tixl", "string", "repeat", "combine", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoText myRepeatText(VuoText fragment, VuoInteger count)
{
	if (!VuoText_isPopulated(fragment))
		return VuoText_make("");
	if (count < 0)
		count = 0;
	if (count > 1000)
		count = 1000;
	if (count == 0)
		return VuoText_make("");

	size_t fragmentLength = strlen(fragment);
	char *result = (char *)malloc(fragmentLength * (size_t)count + 1);
	char *cursor = result;
	for (VuoInteger i = 0; i < count; ++i)
	{
		memcpy(cursor, fragment, fragmentLength);
		cursor += fragmentLength;
	}
	*cursor = '\0';
	return VuoText_makeWithoutCopying(result);
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) fragment,
		VuoInputData(VuoInteger, {"default":5}) count,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	*result = myRepeatText(fragment, count);
}
