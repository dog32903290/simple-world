/**
 * @file
 * my.string.convert.intToString node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/convert/IntToString.
 */

#include "VuoText.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_IntToString",
					 "description" : "TiXL IntToString adapter. Source: external/tixl/Operators/Lib/string/convert/IntToString.cs. Category: Operators/Lib/string/convert. Primary output: string (ColorForString #779552). This is adapter-bounded for full .NET string.Format parity; it supports empty format, {0}, {0:0}, and prefix/suffix around that token.",
					 "keywords" : [ "tixl", "string", "convert", "int", "format", "adapter-bounded", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoText myFormatInteger(VuoInteger value, VuoText format)
{
	if (!VuoText_isPopulated(format))
		return VuoText_makeWithoutCopying(VuoText_format("%lld", value));
	if (strstr(format, "{1") || !strstr(format, "{0"))
		return VuoText_make("Invalid Format");

	const char *open = strstr(format, "{0");
	const char *close = open ? strchr(open, '}') : NULL;
	if (!open || !close)
		return VuoText_make("Invalid Format");

	char token[32];
	size_t tokenLength = (size_t)(close - open + 1);
	if (tokenLength >= sizeof(token))
		return VuoText_make("Invalid Format");
	memcpy(token, open, tokenLength);
	token[tokenLength] = '\0';
	if (strcmp(token, "{0}") != 0 && strcmp(token, "{0:0}") != 0)
		return VuoText_make("Invalid Format");

	char formatted[64];
	snprintf(formatted, sizeof(formatted), "%lld", value);
	size_t prefixLength = (size_t)(open - format);
	size_t suffixLength = strlen(close + 1);
	char *result = (char *)malloc(prefixLength + strlen(formatted) + suffixLength + 1);
	memcpy(result, format, prefixLength);
	strcpy(result + prefixLength, formatted);
	strcpy(result + prefixLength + strlen(formatted), close + 1);
	return VuoText_makeWithoutCopying(result);
}

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoInputData(VuoText, {"default":"{0:0}"}) format,
		VuoOutputData(VuoText, {"name":"Output"}) output
)
{
	*output = myFormatInteger(value, format);
}
