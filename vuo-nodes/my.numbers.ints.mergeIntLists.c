/**
 * @file
 * my.numbers.ints.mergeIntLists node implementation.
 *
 * TiXL parity contract:
 * - Visible title: my_MergeIntLists
 * - Category: Operators/Lib/numbers/ints
 * - Source: external/tixl/Operators/Lib/numbers/ints/MergeIntLists.cs
 * - Defaults: Enabled=false, MaxSize=-1, MergeMode=0, StartIndices=[], InputLists=[] from MergeIntLists.t3
 * - Primary output: List<int> Result (ColorForValues #868C8D)
 *
 * Vuo bounded adapter: fixed 3 input lists stand in for TiXL MultiInputSlot<List<int>>.
 * Merge modes preserve TiXL's Append/Htp/Ltp/FailOver/Average laws within that fixed arity.
 */

#include "VuoList_VuoInteger.h"
#include <stdlib.h>
#include <string.h>

VuoModuleMetadata({
					 "title" : "my_MergeIntLists",
					 "description" : "Merges up to three integer lists with TiXL append, HTP, LTP, FailOver, and Average modes.",
					 "keywords" : [ "tixl", "ints", "list", "merge", "append", "htp", "ltp", "failover", "average" ],
					 "version" : "1.0.0",
				 });

typedef struct
{
	VuoInteger *values;
	unsigned long count;
} IntArray;

struct nodeInstanceData
{
	IntArray ltp;
	IntArray previous[3];
	int activeFailoverIndex;
};

static void freeArray(IntArray *array)
{
	free(array->values);
	array->values = NULL;
	array->count = 0;
}

static IntArray copyFromVuo(VuoList_VuoInteger list)
{
	IntArray array = { NULL, 0 };
	if (!list)
		return array;

	array.count = VuoListGetCount_VuoInteger(list);
	if (array.count == 0)
		return array;

	array.values = (VuoInteger *)calloc(array.count, sizeof(VuoInteger));
	for (unsigned long i = 0; i < array.count; ++i)
		array.values[i] = VuoListGetValue_VuoInteger(list, i + 1);
	return array;
}

static void assignCopy(IntArray *target, const IntArray *source)
{
	freeArray(target);
	target->count = source->count;
	if (source->count == 0)
		return;
	target->values = (VuoInteger *)calloc(source->count, sizeof(VuoInteger));
	memcpy(target->values, source->values, sizeof(VuoInteger) * source->count);
}

static bool arraysEqual(const IntArray *a, const IntArray *b)
{
	if (a->count != b->count)
		return false;
	for (unsigned long i = 0; i < a->count; ++i)
		if (a->values[i] != b->values[i])
			return false;
	return true;
}

static VuoList_VuoInteger makeListWithCount(unsigned long count, VuoInteger fill)
{
	VuoList_VuoInteger list = VuoListCreate_VuoInteger();
	for (unsigned long i = 0; i < count; ++i)
		VuoListAppendValue_VuoInteger(list, fill);
	return list;
}

static void appendArray(VuoList_VuoInteger list, const IntArray *source)
{
	for (unsigned long i = 0; i < source->count; ++i)
		VuoListAppendValue_VuoInteger(list, source->values[i]);
}

static VuoList_VuoInteger modeAppend(IntArray sources[3], VuoList_VuoInteger startIndices, VuoInteger maxSize)
{
	bool useMaxSize = maxSize >= 0;
	VuoList_VuoInteger result = useMaxSize ? makeListWithCount((unsigned long)maxSize, 0) : VuoListCreate_VuoInteger();
	VuoInteger writeIndex = 0;
	unsigned long startCount = startIndices ? VuoListGetCount_VuoInteger(startIndices) : 0;

	for (unsigned long listIndex = 0; listIndex < 3; ++listIndex)
	{
		IntArray *source = &sources[listIndex];
		if (source->count == 0)
			continue;

		if (listIndex < startCount)
		{
			VuoInteger newStartIndex = VuoListGetValue_VuoInteger(startIndices, listIndex + 1);
			if (newStartIndex >= 0 && (!useMaxSize || newStartIndex < maxSize))
				writeIndex = newStartIndex;
		}

		for (unsigned long sourceIndex = 0; sourceIndex < source->count; ++sourceIndex)
		{
			if (useMaxSize && writeIndex >= maxSize)
				break;

			if (!useMaxSize)
				while (writeIndex > (VuoInteger)VuoListGetCount_VuoInteger(result))
					VuoListAppendValue_VuoInteger(result, -1);

			VuoListSetValue_VuoInteger(result, source->values[sourceIndex], (unsigned long)writeIndex + 1, true);
			writeIndex++;
		}
	}

	return result;
}

static unsigned long maxSourceLength(IntArray sources[3])
{
	unsigned long maxLength = 0;
	for (unsigned long listIndex = 0; listIndex < 3; ++listIndex)
		if (sources[listIndex].count > maxLength)
			maxLength = sources[listIndex].count;
	return maxLength;
}

static VuoList_VuoInteger modeHtp(IntArray sources[3])
{
	VuoList_VuoInteger result = VuoListCreate_VuoInteger();
	unsigned long maxLength = maxSourceLength(sources);
	for (unsigned long i = 0; i < maxLength; ++i)
	{
		bool found = false;
		VuoInteger maxValue = 0;
		for (unsigned long listIndex = 0; listIndex < 3; ++listIndex)
			if (i < sources[listIndex].count && (!found || sources[listIndex].values[i] > maxValue))
			{
				maxValue = sources[listIndex].values[i];
				found = true;
			}
		VuoListAppendValue_VuoInteger(result, found ? maxValue : 0);
	}
	return result;
}

static VuoList_VuoInteger modeLtp(struct nodeInstanceData *instance, IntArray sources[3])
{
	unsigned long maxLength = maxSourceLength(sources);
	if (instance->ltp.count < maxLength)
	{
		instance->ltp.values = (VuoInteger *)realloc(instance->ltp.values, sizeof(VuoInteger) * maxLength);
		for (unsigned long i = instance->ltp.count; i < maxLength; ++i)
			instance->ltp.values[i] = 0;
		instance->ltp.count = maxLength;
	}

	for (unsigned long listIndex = 0; listIndex < 3; ++listIndex)
		for (unsigned long i = 0; i < sources[listIndex].count; ++i)
			instance->ltp.values[i] = sources[listIndex].values[i];

	VuoList_VuoInteger result = VuoListCreate_VuoInteger();
	appendArray(result, &instance->ltp);
	return result;
}

static VuoList_VuoInteger modeFailOver(struct nodeInstanceData *instance, IntArray sources[3])
{
	bool activeChanged = false;
	if (instance->activeFailoverIndex >= 0 && instance->activeFailoverIndex < 3)
	{
		IntArray *current = &sources[instance->activeFailoverIndex];
		IntArray *previous = &instance->previous[instance->activeFailoverIndex];
		activeChanged = current->count > 0 && !arraysEqual(current, previous);
	}

	if (sources[0].count > 0 && !arraysEqual(&sources[0], &instance->previous[0]))
		instance->activeFailoverIndex = 0;
	else if (!activeChanged)
	{
		bool found = false;
		for (int i = 0; i < 3; ++i)
			if (sources[i].count > 0 && !arraysEqual(&sources[i], &instance->previous[i]))
			{
				instance->activeFailoverIndex = i;
				found = true;
				break;
			}
		if (!found && (instance->activeFailoverIndex < 0 || instance->activeFailoverIndex >= 3 || sources[instance->activeFailoverIndex].count == 0))
			instance->activeFailoverIndex = 0;
	}

	VuoList_VuoInteger result = VuoListCreate_VuoInteger();
	if (instance->activeFailoverIndex >= 0 && instance->activeFailoverIndex < 3)
		appendArray(result, &sources[instance->activeFailoverIndex]);

	for (int i = 0; i < 3; ++i)
		assignCopy(&instance->previous[i], &sources[i]);

	return result;
}

static VuoList_VuoInteger modeAverage(IntArray sources[3])
{
	VuoList_VuoInteger result = VuoListCreate_VuoInteger();
	unsigned long maxLength = maxSourceLength(sources);
	for (unsigned long i = 0; i < maxLength; ++i)
	{
		long sum = 0;
		VuoInteger count = 0;
		for (unsigned long listIndex = 0; listIndex < 3; ++listIndex)
			if (i < sources[listIndex].count)
			{
				sum += sources[listIndex].values[i];
				count++;
			}
		VuoListAppendValue_VuoInteger(result, count > 0 ? (VuoInteger)(sum / count) : 0);
	}
	return result;
}

struct nodeInstanceData * nodeInstanceInit(void)
{
	struct nodeInstanceData * instance = (struct nodeInstanceData *)calloc(1, sizeof(struct nodeInstanceData));
	VuoRegister(instance, free);
	instance->activeFailoverIndex = 0;
	return instance;
}

void nodeInstanceEvent
(
		VuoInstanceData(struct nodeInstanceData *) instance,
		VuoInputData(VuoBoolean, {"default":false}) enabled,
		VuoInputData(VuoList_VuoInteger) inputList1,
		VuoInputData(VuoList_VuoInteger) inputList2,
		VuoInputData(VuoList_VuoInteger) inputList3,
		VuoInputData(VuoList_VuoInteger) startIndices,
		VuoInputData(VuoInteger, {"default":-1}) maxSize,
		VuoInputData(VuoInteger, {"default":0, "suggestedMin":0, "suggestedMax":4}) mergeMode,
		VuoOutputData(VuoList_VuoInteger, {"name":"Result"}) result
)
{
	IntArray sources[3] = { copyFromVuo(inputList1), copyFromVuo(inputList2), copyFromVuo(inputList3) };

	if (!enabled || mergeMode == 0)
		*result = modeAppend(sources, startIndices, maxSize);
	else if (mergeMode == 1)
		*result = modeHtp(sources);
	else if (mergeMode == 2)
		*result = modeLtp(*instance, sources);
	else if (mergeMode == 3)
		*result = modeFailOver(*instance, sources);
	else if (mergeMode == 4)
		*result = modeAverage(sources);
	else
		*result = modeAppend(sources, startIndices, maxSize);

	for (int i = 0; i < 3; ++i)
		freeArray(&sources[i]);
}

void nodeInstanceFini(VuoInstanceData(struct nodeInstanceData *) instance)
{
	freeArray(&(*instance)->ltp);
	for (int i = 0; i < 3; ++i)
		freeArray(&(*instance)->previous[i]);
}
