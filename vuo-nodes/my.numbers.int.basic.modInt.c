/**
 * @file
 * my.numbers.int.basic.modInt node implementation.
 *
 * Vuo body-layer adapter for TiXL Operators/Lib/numbers/int/basic/ModInt.
 */

VuoModuleMetadata({
					 "title" : "my_ModInt",
					 "description" : "TiXL ModInt integer adapter. Source: external/tixl/Operators/Lib/numbers/int/basic/ModInt.cs. Category: Operators/Lib/numbers/int/basic. Primary output: int (ColorForValues #868C8D). This is adapter-bounded for mod=0 because TiXL returns without updating the existing slot value, while this stateless Vuo node emits 0.",
					 "keywords" : [ "tixl", "numbers", "int", "basic", "modulo", "remainder", "adapter-bounded", "ColorForValues", "#868C8D" ],
					 "version" : "1.0.0",
					 "dependencies" : [ ],
				 });

void nodeEvent
(
		VuoInputData(VuoInteger, {"default":0}) value,
		VuoInputData(VuoInteger, {"default":1}) mod,
		VuoOutputData(VuoInteger, {"name":"Result"}) result
)
{
	*result = mod == 0 ? 0 : value % mod;
}
