/**
 * @file
 * my.string.search.subString node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/search/SubString.
 */

#include "VuoText.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_SubString",
					 "description" : "TiXL SubString adapter. Source: external/tixl/Operators/Lib/string/search/SubString.cs. Category: Operators/Lib/string/search. Primary output: string (ColorForString #779552).",
					 "keywords" : [ "tixl", "string", "search", "substring", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger clamp(VuoInteger value, VuoInteger min, VuoInteger max)
{
	return value < min ? min : (value > max ? max : value);
}

static VuoText mySubstringBytes(VuoText inputText, VuoInteger start, VuoInteger length)
{
	if (!VuoText_isPopulated(inputText))
		return VuoText_make("");
	VuoInteger textLength = (VuoInteger)strlen(inputText);
	VuoInteger clampStart = clamp(start, 0, textLength);
	VuoInteger clampedLength = clamp(length, 0, textLength - clampStart);
	if (clampedLength == 0 || clampStart >= textLength)
		return VuoText_make("");
	if (start == 0 && length >= textLength)
		return VuoText_make(inputText);

	char *copy = (char *)malloc((size_t)clampedLength + 1);
	memcpy(copy, inputText + clampStart, (size_t)clampedLength);
	copy[clampedLength] = '\0';
	return VuoText_makeWithoutCopying(copy);
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) inputText,
		VuoInputData(VuoInteger, {"default":0}) start,
		VuoInputData(VuoInteger, {"default":10000}) length,
		VuoOutputData(VuoText, {"name":"Result"}) result
)
{
	*result = mySubstringBytes(inputText, start, length);
}
