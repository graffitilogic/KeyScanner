#pragma once
#include <vector>
class CPURandomHelper
{
	static std::vector<uint64_t> getCPURndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size);
	static std::vector<uint64_t> getCPURndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size);
};

