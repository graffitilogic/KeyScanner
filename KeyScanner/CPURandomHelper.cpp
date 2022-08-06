#include "CPURandomHelper.h"
#include <random>


std::vector<uint64_t> CPURandomHelper::getCPURndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size) {
	// Generate Random using CPU and Standard Engine
	std::random_device rd;
	std::default_random_engine* eng = new std::default_random_engine(rd());
	eng->seed(seed);

	std::uniform_int_distribution<uint64_t>* dist = new std::uniform_int_distribution<uint64_t>(min_value, max_value);

	std::vector<uint64_t> h_vec;//(size);

	uint64_t ct = 0;

	while (ct <= size) {
		uint64_t value = (*dist)(*eng);
		h_vec.push_back(value);
	}

	delete eng;
	delete dist;

	return h_vec;
}

std::vector<uint64_t> CPURandomHelper::getCPURndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size) {
	std::vector<uint64_t> results;

	while (results.size() <= size) {
		std::vector<uint64_t> rndBuffer = getCPURndBatch(seed, 0, values.size() - 1, size);
		for (uint64_t i : rndBuffer) {
			uint64_t value = values[i];
			//printf("%d \n", i );
			results.push_back(value);
			if (results.size() == size) break;;
		}
	}
	return results;
}