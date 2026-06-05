/**
 * @file
 * my.numbers.data.utils.selectVec3FromDict node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_SelectVec3FromDict
 * - Category: Operators/Lib/numbers/data/utils
 * - Source: external/tixl/Operators/Lib/numbers/data/utils/SelectVec3FromDict.cs
 * - Default: DictionaryInput=null, SelectX="" from SelectVec3FromDict.t3
 * - Primary output: Vector3 Result (ColorForValues #868C8D)
 *
 * Body-layer adapter: VuoText key=value dictionary stands in for TiXL Dict<float>.
 * SelectX starts a sorted 3-key run; each selected key must differ from SelectX by <=1 char.
 */

#include "VuoPoint3d.h"
#include "VuoText.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	char *key;
	VuoReal value;
} DictPair;

VuoModuleMetadata({
					 "title" : "my_SelectVec3FromDict",
					 "description" : "TiXL SelectVec3FromDict adapter. Source: external/tixl/Operators/Lib/numbers/data/utils/SelectVec3FromDict.cs. Body-layer adapter: VuoText key=value dictionary stands in for TiXL Dict<float>. Primary output: Vector3 Result (ColorForValues #868C8D).",
					 "keywords" : [ "tixl", "dict", "vec3", "select", "ColorForValues", "#868C8D" ],
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

static char *copySpan(const char *start, const char *end)
{
	size_t length = (size_t)(end - start);
	char *copy = (char *)malloc(length + 1);
	memcpy(copy, start, length);
	copy[length] = 0;
	return copy;
}

static int comparePairsByKey(const void *a, const void *b)
{
	const DictPair *left = (const DictPair *)a;
	const DictPair *right = (const DictPair *)b;
	return strcmp(left->key, right->key);
}

static int differentCharCount(const char *a, const char *b)
{
	size_t lengthA = strlen(a);
	size_t lengthB = strlen(b);
	if (lengthA != lengthB)
		return INT_MAX;

	int count = 0;
	for (size_t i = 0; i < lengthA; ++i)
		if (a[i] != b[i])
			++count;
	return count;
}

static void freePairs(DictPair *pairs, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		free(pairs[i].key);
	free(pairs);
}

static DictPair *parsePairs(VuoText dictionaryInput, size_t *pairCount)
{
	*pairCount = 0;
	if (!dictionaryInput)
		return NULL;

	size_t capacity = 8;
	DictPair *pairs = (DictPair *)calloc(capacity, sizeof(DictPair));
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
			if (keyEnd > keyStart && parseEnd != valueStart)
			{
				if (*pairCount == capacity)
				{
					capacity *= 2;
					pairs = (DictPair *)realloc(pairs, capacity * sizeof(DictPair));
				}
				pairs[*pairCount].key = copySpan(keyStart, keyEnd);
				pairs[*pairCount].value = (VuoReal)parsed;
				++(*pairCount);
			}
		}

		cursor = *partEnd ? partEnd + 1 : partEnd;
	}

	qsort(pairs, *pairCount, sizeof(DictPair), comparePairsByKey);
	return pairs;
}

void nodeEvent
(
		VuoInputData(VuoText, {"default":""}) dictionaryInput,
		VuoInputData(VuoText, {"default":""}) selectX,
		VuoInputData(VuoPoint3d, {"default":{"x":0.0,"y":0.0,"z":0.0}}) previousResult,
		VuoOutputData(VuoPoint3d, {"name":"Result"}) result
)
{
	*result = previousResult;
	if (!selectX || !*selectX)
		return;

	size_t pairCount = 0;
	DictPair *pairs = parsePairs(dictionaryInput, &pairCount);
	for (size_t startIndex = 0; startIndex < pairCount; ++startIndex)
	{
		if (strcmp(pairs[startIndex].key, selectX) != 0)
			continue;

		if (startIndex + 2 < pairCount
			&& differentCharCount(pairs[startIndex].key, selectX) <= 1
			&& differentCharCount(pairs[startIndex + 1].key, selectX) <= 1
			&& differentCharCount(pairs[startIndex + 2].key, selectX) <= 1)
		{
			*result = (VuoPoint3d){pairs[startIndex].value, pairs[startIndex + 1].value, pairs[startIndex + 2].value};
		}
		break;
	}
	freePairs(pairs, pairCount);
}
