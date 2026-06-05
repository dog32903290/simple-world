/**
 * @file
 * my.numbers.float.logic.valueToRate node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/float/logic/ValueToRate.
 */

#include "VuoText.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_ValueToRate",
					 "description" : "TiXL ValueToRate adapter. Source: external/tixl/Operators/Lib/numbers/float/logic/ValueToRate.cs. Category: Operators/Lib/numbers/float/logic. Primary output: float (ColorForValues #868C8D). Parses newline-separated invariant decimal ratios, clamps Value to 0..0.99, then chooses round((count-1)*Value).",
					 "keywords" : [ "tixl", "numbers", "float", "logic", "rate", "ratio", "newline", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static bool myParseLineAsReal(const char *line, VuoReal *value)
{
	while (*line && isspace((unsigned char)*line) && *line != '\n')
		++line;

	char *end = NULL;
	errno = 0;
	VuoReal parsed = strtod(line, &end);
	if (line == end || errno == ERANGE)
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

static size_t myParseRatios(VuoText rates, VuoReal **ratios)
{
	*ratios = NULL;
	if (!VuoText_isPopulated(rates))
		return 0;

	size_t capacity = 8;
	size_t count = 0;
	VuoReal *values = (VuoReal *)malloc(sizeof(VuoReal) * capacity);
	char *copy = strdup(rates);
	char *line = copy;

	for (char *cursor = copy;; ++cursor)
	{
		if (*cursor == '\n' || *cursor == '\0')
		{
			char saved = *cursor;
			*cursor = '\0';
			VuoReal parsed = 0.0;
			if (myParseLineAsReal(line, &parsed))
			{
				if (count == capacity)
				{
					capacity *= 2;
					values = (VuoReal *)realloc(values, sizeof(VuoReal) * capacity);
				}
				values[count++] = parsed;
			}
			if (saved == '\0')
				break;
			line = cursor + 1;
		}
	}

	free(copy);
	*ratios = values;
	return count;
}

static VuoReal myClampValue(VuoReal value)
{
	if (value < 0.0)
		return 0.0;
	if (value > 0.99)
		return 0.99;
	return value;
}

void nodeEvent
(
		VuoInputData(VuoReal, {"default":0.5}) value,
		VuoInputData(VuoText, {"default":"0\n0.0625\n0.125\n0.25\n0.5\n1\n1\n4\n8\n16\n32"}) rates,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal *ratios = NULL;
	size_t count = myParseRatios(rates, &ratios);
	if (count == 0)
	{
		free(ratios);
		*result = 1.0;
		return;
	}

	VuoReal clampedValue = myClampValue(value);
	size_t ratioIndex = (size_t)((count - 1) * clampedValue + 0.5);
	if (ratioIndex >= count)
		ratioIndex = count - 1;

	*result = ratios[ratioIndex];
	free(ratios);
}
