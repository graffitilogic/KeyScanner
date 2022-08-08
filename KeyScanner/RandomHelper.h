#include "../KeyScanner/Secp256k1/secp256k1.h"
#include <vector>
#include <thrust/host_vector.h>
#include <thrust/generate.h>
#include <thrust/random.h>
//#include <thrust/sort.h>
//#include <thrust/functional.h>

#pragma once
class RandomHelper
{

private:

	//static RandomThrustDefault rndDefault;

public:

		static int randTest();

		static secp256k1::uint256 getDefaultRandomRange(secp256k1::uint256 min, secp256k1::uint256 max);

		static std::vector<unsigned int> RandomHelper::getStaticAssemblyBuffer();
		static std::vector<std::vector<unsigned int>> RandomHelper::getRandomizers(uint64_t seed, uint64_t len, uint64_t width);

		static std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool);
		static std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool, bool allowDupes);

		static std::vector<unsigned int> getRndBuffer16(uint64_t buffer_len);
		static std::vector<unsigned int> getRndBuffer32(uint64_t buffer_len);

		static std::vector<uint64_t> RandomHelper::randRange(uint64_t buffer_len, uint64_t min, uint64_t max);
		static std::vector<uint64_t> RandomHelper::sequentialRange(uint64_t buffer_len, uint64_t min, uint64_t max, bool reversed);

		static std::vector<uint64_t> getRndRange(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size);
		static std::vector<uint64_t> getRndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size);

		static thrust::host_vector<uint64_t> getRndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size);

		static std::vector<uint64_t> getCPURndBatch(uint64_t seed, uint64_t min_value, uint64_t max_value, uint64_t size);
		static std::vector<uint64_t> getCPURndRange(uint64_t seed, std::vector<uint64_t> values, uint64_t size);

		static std::vector<secp256k1::uint256> sortKeys(std::vector<secp256k1::uint256> keys);

		static std::vector<secp256k1::uint256> getDistances(std::vector<secp256k1::uint256> keys);

};
