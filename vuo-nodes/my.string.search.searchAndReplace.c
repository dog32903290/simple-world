/**
 * @file
 * my.string.search.searchAndReplace node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/search/SearchAndReplace.
 */

#include "VuoText.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_SearchAndReplace",
					 "description" : "TiXL SearchAndReplace adapter. Source: external/tixl/Operators/Lib/string/search/SearchAndReplace.cs. Category: Operators/Lib/string/search. Primary output: string (ColorForString #779552). This is adapter-bounded for regex mode; non-regex ordinal replace and escaped newline replacement are implemented.",
					 "keywords" : [ "tixl", "string", "search", "replace", "regex", "adapter-bounded", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoText myExpandEscapedNewlines(VuoText text)
{
	if (!VuoText_isPopulated(text))
		return VuoText_make("");
	size_t length = strlen(text);
	char *expanded = (char *)malloc(length + 1);
	char *out = expanded;
	for (const char *in = text; *in; ++in)
	{
		if (in[0] == '\\' && in[1] == 'n')
		{
			*out++ = '\n';
			++in;
		}
		else
			*out++ = *in;
	}
	*out = '\0';
	return VuoText_makeWithoutCopying(expanded);
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) originalString,
		VuoInputData(VuoText, {"default":""}) searchPattern,
		VuoInputData(VuoText, {"default":""}) replace,
		VuoInputData(VuoBoolean, {"default":false}) useRegex,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	if (!VuoText_isPopulated(originalString) || !VuoText_isPopulated(searchPattern) || !VuoText_isPopulated(replace))
	{
		*result = VuoText_make(originalString ? originalString : "");
		return;
	}
	VuoText replacement = myExpandEscapedNewlines(replace);
	// Regex mode is adapter-bounded: Vuo emits the same visible non-regex replacement instead of .NET Regex.Replace.
	(void)useRegex;
	*result = VuoText_replace(originalString, searchPattern, replacement);
}
