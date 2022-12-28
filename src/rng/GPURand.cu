#include "GPURand.h"
#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <set>
#include <thrust/sort.h>
#include <thrust/functional.h>
#include <thrust/copy.h>

using namespace Random;

std::vector<unsigned int>  RandomHelper::getStaticAssemblyBuffer(unsigned int min, unsigned int max) {

	std::vector<unsigned int> result;

	//unsigned int max = 0xffff;
	unsigned int ui = min;
	while (ui <= max) {
		result.push_back(ui);
		ui++;
	}


	return result;
}

std::vector<std::vector<unsigned int>> RandomHelper::getRandomizers(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width) {
	return getRndBuffer(seed, min, max, len, width, false);
}

std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool) {

	std::vector<std::vector<unsigned int>> results;

	std::vector<uint64_t> seedMods = RandomHelper::randRange(width, min, max);

	for (int64_t i = 0; i < width; i++) {
		if (!singlePool || i == 0) {
			//time for the devrnd seed

			thrust::default_random_engine rng(seed * seedMods[i]);

			thrust::uniform_int_distribution<unsigned int> dist(min, max);

			//thrust::host_vector<unsigned int> h_vec(buffer_len);
			std::vector<unsigned int> result(len);

			thrust::generate(result.begin(), result.end(), [&] { return dist(rng); });

			// Transfer data to the device.
			thrust::device_vector<unsigned int> d_vec = result; //h_vec;

			// Transfer data back to host.
			thrust::copy(d_vec.begin(), d_vec.end(), result.begin());

			results.push_back(result);
		}
		else {
			//make a copy instead of generate
			std::vector<unsigned int> result = results[0];
			results.push_back(result);
		}
	}

	return results;

}

std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool, bool allowDupes) {
	std::vector<std::vector<unsigned int>> results = getRndBuffer(seed, min, max, len, width, singlePool);

	if (!allowDupes) {
		for (int i = 0; i < results.size(); i++) {
			std::vector<unsigned int> draftResults = results[i];
			std::set<unsigned int> dedupe;
			for (int j = 0; j < draftResults.size(); j++) {
				dedupe.insert(draftResults[j]);
			}
			std::vector<unsigned int> finalResults;
			std::copy(dedupe.begin(), dedupe.end(), std::inserter(finalResults, finalResults.end()));
			results[i] = finalResults;
		}
	}
	return results;
}

std::vector<uint64_t> RandomHelper::randRange(uint64_t buffer_len, uint64_t min, uint64_t max) {
	//time for the devrnd seed
	time_t now = time(nullptr);
	time_t mnow = now * 1000;

	// Generate 32M random numbers serially.
	thrust::default_random_engine rng(mnow);

	thrust::uniform_int_distribution<uint64_t> dist(min, max);

	//thrust::host_vector<unsigned int> h_vec(buffer_len);
	std::vector<uint64_t> results(buffer_len);

	thrust::generate(results.begin(), results.end(), [&] { return dist(rng); });

	// Transfer data to the device.
	thrust::device_vector<uint64_t> d_vec = results; //h_vec;


	// Transfer data back to host.
	thrust::copy(d_vec.begin(), d_vec.end(), results.begin());

	return results;
}
