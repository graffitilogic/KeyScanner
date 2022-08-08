#include "RandomHelper.h"
#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <set>
#include <thrust/sort.h>
#include <thrust/functional.h>
#include <thrust/copy.h>




int RandomHelper::randTest()
{
	int MAX_VALUE = 64535;

	// Generate 32M random numbers serially.
	thrust::default_random_engine rng(1337);
	thrust::uniform_int_distribution<int> dist;
	thrust::host_vector<int> h_vec(32 << 20);
	thrust::generate(h_vec.begin(), h_vec.end(), [&] { return dist(rng); });

	// Transfer data to the device.
	thrust::device_vector<int> d_vec = h_vec;

	// Sort data on the device.
	//thrust::sort(d_vec.begin(), d_vec.end());

	// Transfer data back to host.
	thrust::copy(d_vec.begin(), d_vec.end(), h_vec.begin());

	std::vector<int> results;
	for (int i : h_vec) {
		if (i <= MAX_VALUE) results.push_back(i);
	}

	
	return  EXIT_SUCCESS;

}

std::vector<unsigned int> RandomHelper::getStaticAssemblyBuffer() {

	std::vector<unsigned int> result;

	unsigned int max = 0xffff;
	unsigned int ui = 0;
	while (ui <= max) {
		result.push_back(ui);
		ui++;
	}


	return result;
}

std::vector<std::vector<unsigned int>> RandomHelper::getRandomizers(uint64_t seed, uint64_t len, uint64_t width) {
	return getRndBuffer(seed, 0, 0xffff, len, width, false);
}

std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool) {

	std::vector<std::vector<unsigned int>> results;

	for (int64_t i = 0; i < width; i++) {
		if (!singlePool || i == 0) {
			//time for the devrnd seed

			thrust::default_random_engine rng(seed * i);

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

std::vector<unsigned int> RandomHelper::getRndBuffer16(uint64_t buffer_len) {
	//time for the devrnd seed
	time_t now = time(nullptr);
	time_t mnow = now * 1000;

	// Generate 32M random numbers serially.
	thrust::default_random_engine rng(mnow);

	thrust::uniform_int_distribution<unsigned int> dist(0, 0xffff);

	//thrust::host_vector<unsigned int> h_vec(buffer_len);
	std::vector<unsigned int> results(buffer_len);

	thrust::generate(results.begin(), results.end(), [&] { return dist(rng); });

	// Transfer data to the device.
	thrust::device_vector<unsigned int> d_vec = results; //h_vec;


	// Transfer data back to host.
	thrust::copy(d_vec.begin(), d_vec.end(), results.begin());

	return results;
}
std::vector<unsigned int> RandomHelper::getRndBuffer32(uint64_t buffer_len) {
	//time for the devrnd seed
	time_t now = time(nullptr);
	time_t mnow = now * 1000;

	// Generate 32M random numbers serially.
	thrust::default_random_engine rng(mnow);

	thrust::uniform_int_distribution<unsigned int> dist(0, 0xffffffff);

	//thrust::host_vector<unsigned int> h_vec(buffer_len);
	std::vector<unsigned int> results(buffer_len);

	thrust::generate(results.begin(), results.end(), [&] { return dist(rng); });

	// Transfer data to the device.
	thrust::device_vector<unsigned int> d_vec = results; //h_vec;


	// Transfer data back to host.
	thrust::copy(d_vec.begin(), d_vec.end(), results.begin());

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


std::vector<uint64_t> RandomHelper::sequentialRange(uint64_t buffer_len, uint64_t min, uint64_t max, bool reversed) {

	std::vector<uint64_t> results;
	uint64_t count = 0;
	uint64_t running_value = reversed ? max : min;


		while (count < buffer_len) {
			results.push_back(running_value);
			count++;
			//conditionally iterate
			if (reversed) {
				running_value--;
				if (running_value == min) running_value = max;
			}
			else {
				running_value++;
				if (running_value = max) running_value = min;
			}
		}

	return results;
}

std::vector<uint64_t> RandomHelper::getRndRange(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size) {
	std::vector<uint64_t> results;

	while (results.size() <= size) {
		thrust::host_vector<uint64_t> rndBuffer = getRndBatch(seed, min_value, max_value, size);
		for (uint64_t i : rndBuffer) {
			//printf("%d \n", i );
			if (i <= max_value) results.push_back(i);
			if (results.size() == size) break;;
		}
	}
	return results;
}

std::vector<uint64_t> RandomHelper::getRndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size) {
	std::vector<uint64_t> results;

	while (results.size() <= size) {
		thrust::host_vector<uint64_t> rndBuffer = getRndBatch(seed, 0, values.size()-1, size);
		for (uint64_t i : rndBuffer) {
			uint64_t value = values[i];
			//printf("%d \n", i );
			results.push_back(value);
			if (results.size() == size) break;;
		}
	}
	return results;
}

thrust::host_vector<uint64_t> RandomHelper::getRndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size) {
	// Generate 32M random numbers serially.
	thrust::default_random_engine rng(seed);
	thrust::uniform_int_distribution<uint64_t> dist(min_value, max_value);
	//thrust::host_vector<int> h_vec(32 << 20);
	thrust::host_vector<uint64_t> h_vec(size);
	thrust::generate(h_vec.begin(), h_vec.end(), [&] { return dist(rng); });

	// Transfer data to the device.
	thrust::device_vector<uint64_t> d_vec = h_vec;

	// Sort data on the device.  - sort freaks out, I think b/c of sort namespace colisions 
	//thrust::sort(d_vec.begin(), d_vec.end());

	// Transfer data back to host.
	thrust::copy(d_vec.begin(), d_vec.end(), h_vec.begin());

	dist.reset();

	return h_vec;
}


secp256k1::uint256 RandomHelper::getDefaultRandomRange(secp256k1::uint256 min, secp256k1::uint256 max)
{
	secp256k1::uint256 result;
	secp256k1::uint256 range = max.sub(min);

	unsigned char targetByteSize = (range.getBitRange() + 31) / 32;

	for (int i = 0; i < 8; i++) {
		if (targetByteSize > i) {
			result.v[i] = 0; //CudaRng::rndDefault.getChunk();
			if (targetByteSize > i && targetByteSize <= i + 1 && range.v[i] != 0 && result.v[i] > range.v[i]) {
				result.v[i] %= range.v[i];
			}
		}
	}

	return result.add(min);
}

std::vector<uint64_t> RandomHelper::getCPURndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size) {
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
		ct++;
	}
	
	delete eng;
	delete dist;

	return h_vec;
}

std::vector<uint64_t> RandomHelper::getCPURndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size) {
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


std::vector<secp256k1::uint256> RandomHelper::sortKeys(std::vector<secp256k1::uint256> keys) {
	std::vector<secp256k1::uint256> results;
	thrust::host_vector<secp256k1::uint256> hostKeys;
	//thrust::device_vector<secp256k1::uint256> deviceKeys = keys;

	for (int k = 0; k < keys.size(); k++) {
		hostKeys.push_back(keys[k]);
	}

	//thrust::copy(keys.begin(), keys.end(), hostKeys.begin());
	thrust::sort(hostKeys.begin(), hostKeys.end());
	//thrust::sort(deviceKeys.begin(), deviceKeys.end());

	// Transfer data back to host.
	//thrust::copy(deviceKeys.begin(), deviceKeys.end(), results.begin());
	//thrust::copy(hostKeys.begin(), hostKeys.end(), results.begin());

	for (int k = 0; k < hostKeys.size(); k++) {
		results.push_back(hostKeys[k]);
	}

	return results;
}


std::vector<secp256k1::uint256> RandomHelper::getDistances(std::vector<secp256k1::uint256> keys) {
	std::vector<secp256k1::uint256> results;
	thrust::host_vector<secp256k1::uint256> hostKeys;

	//iterate through the keys, determine the intra-key distances
	secp256k1::uint256 lastKey;
	for (int k = 0; k < keys.size(); k++) {
		if (k > 0) {
			results.push_back(keys[k] - lastKey);
		}
		else {
			results.push_back(0);
		}
		lastKey = keys[k];
	}

	return results;
}

