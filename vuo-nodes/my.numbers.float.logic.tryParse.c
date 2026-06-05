/**
 * @file
 * my.numbers.float.logic.tryParse node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/logic/TryParse.
 */

#include "VuoText.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

VuoModuleMetadata({
					 "title" : "my_TryParse",
					 "description" : "TiXL TryParse adapter. Source: external/tixl/Operators/Lib/numbers/float/logic/TryParse.cs. Category: Operators/Lib/numbers/float/logic. Primary output: float (ColorForValues #868C8D). Parses plain decimal text and returns Default when parsing fails; adapter-bounded for full .NET current-culture float.TryParse parity.",
					 "keywords" : [ "tixl", "numbers", "float", "logic", "tryparse", "parse", "string", "default", "adapter-bounded", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static bool myParseReal(VuoText text, VuoReal *value)
{
	if (!text)
		return false;

	char *end = NULL;
	errno = 0;
	VuoReal parsed = strtod(text, &end);
	if (text == end || errno == ERANGE)
		return false;

	while (end && *end)
	{
		if (!isspace((unsigned char)*end))
			return false;
		++end;
	}

	*value = parsed;
	return true;
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) string,
		VuoInputData(VuoReal, {"default":0.0, "name":"Default"}) defaultValue,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal parsed = 0.0;
	*result = myParseReal(string, &parsed) ? parsed : defaultValue;
}
