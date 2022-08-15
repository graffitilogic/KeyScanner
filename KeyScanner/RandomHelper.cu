#include "RandomHelper.h"
#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <set>
#include <thrust/sort.h>
#include <thrust/functional.h>
#include <thrust/copy.h>


using namespace Random;

std::vector<unsigned int> RandomHelper::getStaticAssemblyBuffer(unsigned int min, unsigned int max) {

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

	std::vector<uint64_t> seedMods = RandomHelper::randRange(width, 0, 69000000);
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

	while (results.size() < size) {
		thrust::host_vector<uint64_t> rndBuffer = getRndBatch(seed, min_value, max_value, size);
		for (uint64_t i : rndBuffer) {
			//printf("%d \n", i );
			if (i <= max_value) results.push_back(i);
			if (results.size() == size) break;
		}
		if (results.size() == size) break;
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


std::vector<secp256k1::uint256> RandomHelper::getDistances(std::vector<secp256k1::uint256> keys, uint64_t truncate) {
	std::vector<secp256k1::uint256> results;

	//iterate through the keys, determine the intra-key distances
	//uint32_t scaler = std::pow(16, truncate);

	//secp256k1::uint256 scaler256 = secp256k1::uint256("4096");
	secp256k1::uint256 lastKey;
	secp256k1::uint256 thisKey;
	secp256k1::uint256 thisDistance;
	secp256k1::uint256 referenceDistance;
	for (int k = 0; k < keys.size(); k++) {

		//Logger::log(LogLevel::Debug, "ThisK1: " + _startingKeys.Keys[k].toString());
		thisKey = keys[k];
		//thisKey = keys[k].div(scaler);
		//thisKey = keys[k].rShift(scaler);

		//thisKey = keys[k].div(;

		if (k > 0) {
			//truncate the end of the comparison key - so we are measuruing random-only differences not including a right-padded sequential
			thisDistance = thisKey.sub(lastKey);

			//pad the right of the distance so that it can be cleanly added without disturbing the sequentials
			//thisDistance = thisDistance.mul(scaler);

			if (thisDistance == 0) {
				thisDistance = referenceDistance;
			}
			else {
				referenceDistance = thisDistance;
			}

			results.push_back(thisDistance);
		}
		else {
			results.push_back(0);
		}
		lastKey = thisKey;
	}

	return results;
}

secp256k1::uint256 RandomHelper::getDistanceAverage(std::vector<secp256k1::uint256> keys) {
	secp256k1::uint256 result;

	secp256k1::uint256 thisDistance;
	secp256k1::uint256 distanceSum;
	secp256k1::uint256 keyCount = secp256k1::uint256(0);
	secp256k1::uint256 one = secp256k1::uint256(1);

	for (int k = 0; k < keys.size(); k++) {

		thisDistance = keys[k];
		distanceSum = distanceSum.add(thisDistance);
		keyCount = keyCount.add(one);
	}

	result = distanceSum.div(keyCount);
	return result;
}

secp256k1::uint256 RandomHelper::getDistanceMean(std::vector<secp256k1::uint256> keys) {
	secp256k1::uint256 result;

	uint64_t startKey = keys.size() * 0.25;
	uint64_t endKey = keys.size() - startKey;


	secp256k1::uint256 thisDistance;
	secp256k1::uint256 distanceSum;
	secp256k1::uint256 keyCount = secp256k1::uint256(0);
	secp256k1::uint256 one = secp256k1::uint256(1);

	for (int k = startKey; k < endKey; k++) {

		thisDistance = keys[k];
		distanceSum = distanceSum.add(thisDistance);
		keyCount = keyCount.add(one);
	}

	result = distanceSum.div(keyCount);
	return result;
}


