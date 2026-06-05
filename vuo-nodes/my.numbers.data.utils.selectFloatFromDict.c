/**
 * @file
 * my.numbers.data.utils.selectFloatFromDict node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SelectFloatFromDict
 * - Category: Operators/Lib/numbers/data/utils
 * - Source: external/tixl/Operators/Lib/numbers/data/utils/SelectFloatFromDict.cs
 * - Default: DictionaryInput=null, Select="" from SelectFloatFromDict.t3
 * - Primary output: float Result (ColorForValues #868C8D)
 *
 * Body-layer adapter: VuoText key=value dictionary stands in for TiXL Dict<float>.
 * Missing keys return previousResult, exposing TiXL's "unchanged Result.Value" behavior.
 */

#include "VuoText.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_SelectFloatFromDict",
					 "description" : "TiXL SelectFloatFromDict adapter. Source: external/tixl/Operators/Lib/numbers/data/utils/SelectFloatFromDict.cs. Body-layer adapter: VuoText key=value dictionary stands in for TiXL Dict<float>. Primary output: float Result (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "dict", "float", "select", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
				 });

static const char *trimStart(const char *text)
{
	while (*text && isspace((unsigned char)*text))
		++text;
	return text;
}

static const char *trimEnd(const char *start, const char *end)
{
	while (end > start && isspace((unsigned char)*(end - 1)))
		--end;
	return end;
}

static bool keyEquals(const char *start, const char *end, const char *select)
{
	size_t length = (size_t)(end - start);
	return strlen(select) == length && strncmp(start, select, length) == 0;
}

static bool myDictLookup(VuoText dictionaryInput, VuoText select, VuoReal *value)
{
	if (!dictionaryInput || !select || !*select)
		return false;

	const char *cursor = dictionaryInput;
	while (*cursor)
	{
		const char *partStart = trimStart(cursor);
		const char *partEnd = partStart;
		while (*partEnd && *partEnd != ';' && *partEnd != ',' && *partEnd != '\n')
			++partEnd;

		const char *separator = partStart;
		while (separator < partEnd && *separator != '=' && *separator != ':')
			++separator;

		if (separator < partEnd)
		{
			const char *keyStart = trimStart(partStart);
			const char *keyEnd = trimEnd(keyStart, separator);
			const char *valueStart = trimStart(separator + 1);
			char *parseEnd = NULL;
			double parsed = strtod(valueStart, &parseEnd);
			if (parseEnd != valueStart && keyEquals(keyStart, keyEnd, select))
			{
				*value = (VuoReal)parsed;
				return true;
			}
		}

		cursor = *partEnd ? partEnd + 1 : partEnd;
	}

	return false;
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) dictionaryInput,
		VuoInputData(VuoText, {"default":""}) select,
		VuoInputData(VuoReal, {"default":0.0}) previousResult,
		VuoOutputData(VuoReal, {"name":"Result"}) result
)
{
	VuoReal value = 0.0;
	if (myDictLookup(dictionaryInput, select, &value))
		*result = value;
	else
		*result = previousResult;
}
