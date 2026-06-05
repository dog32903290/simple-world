/**
 * @file
 * my.string.list.splitString node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/list/SplitString.
 */

#include "VuoInteger.h"
#include "VuoList_VuoText.h"
#include "VuoText.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_SplitString",
					 "description" : "TiXL SplitString adapter. Source: external/tixl/Operators/Lib/string/list/SplitString.cs. Category: Operators/Lib/string/list. Primary output: List<string> (ColorForString #779552). Implements TiXL split delimiter law: empty split or literal \\\\n means newline, otherwise the first delimiter character is used. This is adapter-bounded for UTF-16 first-character parity and for TiXL's empty input Count retaining its previous slot value; Vuo emits Count 0.",
					 "keywords" : [ "tixl", "string", "list", "split", "fragments", "adapter-bounded", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static char myDelimiterByte(VuoText split)
{
	if (!split || split[0] == '\0' || strcmp(split, "\\n") == 0)
		return '\n';
	// TiXL uses split[0] on a .NET string; this byte-level adapter is bounded for UTF-16/non-ASCII delimiters.
	return split[0];
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":"."}) stringText,
		VuoInputData(VuoText, {"default":"\\n"}) split,
		VuoOutputData(VuoList_VuoText, {"name":"Fragments"}) fragments,
		VuoOutputData(VuoInteger, {"name":"Count"}) count
)
{
	VuoList_VuoText result = VuoListCreate_VuoText();

	if (!VuoText_isPopulated(stringText))
	{
		// Adapter-bounded: TiXL returns the empty list without writing Count; stateless Vuo emits 0.
		*fragments = result;
		*count = 0;
		return;
	}

	char delimiter = myDelimiterByte(split);
	const char *segmentStart = stringText;
	const char *cursor = stringText;

	for (;; ++cursor)
	{
		if (*cursor == delimiter || *cursor == '\0')
		{
			VuoText fragment = VuoText_makeWithMaxLength(segmentStart, (size_t)(cursor - segmentStart));
			VuoListAppendValue_VuoText(result, fragment);
			if (*cursor == '\0')
				break;
			segmentStart = cursor + 1;
		}
	}

	*fragments = result;
	*count = (VuoInteger)VuoListGetCount_VuoText(result);
}
