/**
 * @file
 * my.numbers.int.process.getAPrime node implementation.
 *
 * Exact Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/process/GetAPrime.
 */

#include <math.h>

VuoModuleMetadata({
					 "title" : "my_GetAPrime",
					 "description" : "TiXL GetAPrime adapter. Source: external/tixl/Operators/Lib/numbers/int/process/GetAPrime.cs. Category: Operators/Lib/numbers/int/process. Primary output: int (ColorForValues #868C8D). Index is 1-based; index < 1 returns -1.",
					 "keywords" : [ "tixl", "numbers", "int", "process", "prime", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

static VuoInteger myComputePrime(VuoInteger index)
{
	if (index < 1)
		return -1;

	VuoInteger count = 0;
	VuoInteger n = 2;
	while (true)
	{
		if (count > 10000)
			return -1;

		bool isPrime = true;
		VuoInteger limit = (VuoInteger)sqrt((double)n);
		for (VuoInteger i = 2; i <= limit; ++i)
		{
			if (n % i == 0)
			{
				isPrime = false;
				break;
			}
		}

		if (isPrime)
		{
			++count;
			if (count == index)
				return n;
		}

		n = n == 2 ? 3 : n + 2;
	}
}

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) index,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = myComputePrime(index);
}
