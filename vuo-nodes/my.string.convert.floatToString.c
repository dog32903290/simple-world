/**
 * @file
 * my.string.convert.floatToString node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/string/convert/FloatToString.
 */

#include "VuoText.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_FloatToString",
					 "description" : "TiXL FloatToString adapter. Source: external/tixl/Operators/Lib/string/convert/FloatToString.cs. Category: Operators/Lib/string/convert. Primary output: string (ColorForString #779552). This is adapter-bounded for full .NET string.Format parity; it supports empty format, {0}, {0:0}, {0:0.0...}, and prefix/suffix around that token.",
					 "keywords" : [ "tixl", "string", "convert", "float", "format", "adapter-bounded", "ColorForString", "#779552" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static int myPrecisionFromToken(const char *token)
{
	const char *dot = strchr(token, '.');
	if (!dot)
		return 0;
	int precision = 0;
	for (const char *cursor = dot + 1; *cursor == '0'; ++cursor)
		++precision;
	return precision;
}

static VuoText myFormatReal(VuoReal value, VuoText format)
{
	if (!VuoText_isPopulated(format))
		return VuoText_makeWithoutCopying(VuoText_format("%g", value));
	if (strstr(format, "{1") || !strstr(format, "{0"))
		return VuoText_make("Invalid Format");

	const char *open = strstr(format, "{0");
	const char *close = open ? strchr(open, '}') : NULL;
	if (!open || !close)
		return VuoText_make("Invalid Format");

	char token[64];
	size_t tokenLength = (size_t)(close - open + 1);
	if (tokenLength >= sizeof(token))
		return VuoText_make("Invalid Format");
	memcpy(token, open, tokenLength);
	token[tokenLength] = '\0';

	char formatted[128];
	if (strcmp(token, "{0}") == 0)
		snprintf(formatted, sizeof(formatted), "%g", value);
	else if (strncmp(token, "{0:0", 4) == 0)
		snprintf(formatted, sizeof(formatted), "%.*f", myPrecisionFromToken(token), value);
	else
		return VuoText_make("Invalid Format");

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
		VuoInputData(VuoReal, {"default":0.0}) value,
		VuoInputData(VuoText, {"default":"{0:0.000}"}) format,
		VuoOutputData(VuoText, {"name":"Output"}) output
)
{
	*output = myFormatReal(value, format);
}
