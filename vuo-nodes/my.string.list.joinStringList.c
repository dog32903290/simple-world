/**
 * @file
 * my.string.list.joinStringList node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/list/JoinStringList.
 */

#include "VuoList_VuoText.h"
#include "VuoText.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_JoinStringList",
					 "description" : "TiXL JoinStringList adapter. Source: external/tixl/Operators/Lib/string/list/JoinStringList.cs. Category: Operators/Lib/string/list. Primary output: string (ColorForString #779552). Expands literal \\\\n in Separator and joins list order exactly. This is adapter-bounded for TiXL IStatusProvider warning output; Vuo emits the Result text only and returns empty text for null/empty input.",
					 "keywords" : [ "tixl", "string", "list", "join", "separator", "adapter-bounded", "IStatusProvider", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoText myExpandEscapedNewlines(VuoText text)
{
	if (!text)
		return NULL;

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
		VuoInputData(VuoList_VuoText) input,
		VuoInputData(VuoText, {"default":"\\n"}) separator,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	if (!separator || !input || VuoListGetCount_VuoText(input) == 0)
	{
		// Adapter-bounded: TiXL also raises an IStatusProvider warning, which Vuo has no equivalent output port for here.
		*result = VuoText_make("");
		return;
	}

	VuoText expandedSeparator = myExpandEscapedNewlines(separator);
	*result = VuoText_appendWithSeparator(input, expandedSeparator, true);
}
