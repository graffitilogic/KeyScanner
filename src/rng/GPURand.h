//#include "../KeyScanner/Secp256k1/secp256k1.h"
#include <vector>
#include "../math/IntGroup.h"
#include <thrust/host_vector.h>
#include <thrust/generate.h>
#include <thrust/random.h>
//#include <thrust/sort.h>
//#include <thrust/functional.h>

namespace Random
{

	class RandomHelper
	{

	private:

		//static RandomThrustDefault rndDefault;

	public:

		//static secp256k1::uint256 getDefaultRandomRange(secp256k1::uint256 min, secp256k1::uint256 max);

		static std::vector<unsigned int> RandomHelper::getStaticAssemblyBuffer(unsigned int min, unsigned int max);
		static std::vector<std::vector<unsigned int>> RandomHelper::getRandomizers(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width);

		static std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool);
		static std::vector<std::vector<unsigned int>> RandomHelper::getRndBuffer(uint64_t seed, unsigned int min, unsigned int max, uint64_t len, uint64_t width, bool singlePool, bool allowDupes);

		static std::vector<uint64_t> RandomHelper::randRange(uint64_t buffer_len, uint64_t min, uint64_t max);

	};

	class GPURand
	{
		std::vector<std::vector<unsigned int>> randomizers;
		std::vector<unsigned int> rand_buffer;

		uint64_t buffer_max;
		uint64_t buffer_index;
		uint64_t randomizer_rotations;

		uint64_t chunkCounter;

		std::vector<Int> prefixes;
		uint64_t prefix_buffer_max;
		uint64_t prefix_index;

		std::vector<Int> incrementors;
		uint64_t incrementor_index;

	private:

		//static RandomThrustDefault rndDefault;

	public:

		GPURand()
		{
			chunkCounter = 0;
			randomizer_rotations = 0;
		}

		void load32BitBuffer(uint64_t seed, uint64_t len) {
			rand_buffer = Random::RandomHelper::getRndBuffer(seed, 0x0, 0xffffffff, len * 10, 1, false)[0];
		}

		void loadStaticPrefixes(Int min, Int max, uint64_t len) {
			std::vector<Int> results;
			Int sixteen = Int::sixteen();

			//reduce min and max until they match length
			while (min.GetBase16().size() > len) {
				min.Div(&sixteen);
			}

			while (max.GetBase16().size() > len) {
				max.Div(&sixteen);
			}

			Int pfx = new Int();
			pfx.Set(&min);

			while (pfx.IsLower(&max)) {
				results.push_back(pfx);
				pfx.AddOne();
			}

			if (results.size() == 0) results.push_back(min);

			prefixes = results;
			prefix_buffer_max = results.size();
			prefix_index = 0;

			reverse(prefixes.begin(), prefixes.end());

		}

		int64_t getStaticPrefixBufferSize() {
			return prefixes.size();
		}

		Int getStaticPrefix(uint64_t index) {
			if (index < prefixes.size()) return prefixes[index];
			
			
			return new Int();
		}

		int64_t getIncrementorsSize() {
			return incrementors.size();
		}

		void setIncrementors(std::vector<Int> ics) {
			incrementors = ics;
			incrementor_index = 1;
		}

		void loadRandomizers(uint64_t seed, uint64_t len, uint64_t width) {
			//clear
			//std::vector<unsigned int>().swap(rand_buffer);
			std::vector<std::vector<unsigned int>>().swap(randomizers);

			//fill
			if (rand_buffer.size()==0) rand_buffer = Random::RandomHelper::getStaticAssemblyBuffer(0x0, 0xffff);
			randomizers = Random::RandomHelper::getRandomizers(seed, 0x0, 0xffff, len, width);
			randomizer_rotations = 0;
		}

		int64_t getRandomizersSize() {
			return randomizers.size();
		}

		bool shiftRandomizers() {
			int n = randomizers.size();  ///size of the array
			int d = 1;				//number of rotations
			
			if (randomizer_rotations >= 3) {// (n/2)) {
				return false;
			}

			// Storing rotated version of array
			std::vector<std::vector<unsigned int>> tmp(n);

			// Keepig track of the current index
			// of temp[]
			int k = 0;

			// Storing the n - d elements of
			// array arr[] to the front of temp[]
			for (int i = d; i < n; i++) {
				tmp[k] = randomizers[i];
				k++;
			}

			// Storing the first d elements of array arr[]
			//  into temp
			for (int i = 0; i < d; i++) {
				tmp[k] = randomizers[i];
				k++;
			}

			// Copying the elements of temp[] in arr[]
			// to get the final rotated array
			for (int i = 0; i < n; i++) {
				randomizers[i] = tmp[i];
			}

			//std::vector<std::vector<unsigned int>>().swap(tmp);
			randomizer_rotations++;
			return true;
		}

		std::vector<std::vector<unsigned int>> getRandomizers() {
			return randomizers;
		}

		unsigned int getChunk(uint64_t col, uint64_t row) {
			//treating them as 2x16 instead of 1x32 for perf reasons.
			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff of length[len]; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;

			uint64_t bufferIndex16a = randomizers[col][row];
			uint64_t bufferIndex16b = randomizers[(randomizers.size()/2) + col][row];

			unsigned int buffer16a = rand_buffer[bufferIndex16a];
			unsigned int buffer16b = rand_buffer[bufferIndex16b];

			if (buffer16a == 0) buffer16a = 1;
			if (buffer16b == 0) buffer16b = 1;

			/*
			//pad the parts, if needed
			while (buffer16a < 10000) {
				buffer16a = buffer16a * 0x10;
			}
	

			while (buffer16b < 10000) {
				buffer16b = buffer16b * 0x10;
			}
			*/

			//unsigned int buffer32 = (buffer16a * 100000) + buffer16b;
			unsigned int buffer32 = (buffer16a * 0x10000) + buffer16b;

			return buffer32;
		}

		unsigned int getChunk32(uint64_t col, uint64_t row) {
			//treating them as 2x16 instead of 1x32 for perf reasons.
			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff of length[len]; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;

			uint64_t bufferIndex = randomizers[col][row];

			unsigned int buffer32 = rand_buffer[bufferIndex];


			if (buffer32 == 0) buffer32 = 1;

			//pad the parts, if needed
			while (buffer32 < 0x10000000) {
				buffer32 = buffer32 * 0x10;
			}
			return buffer32;
		}

		unsigned int getChunk16() {
			//treating them as 2x16 instead of 1x32 for perf reasons.


			unsigned int buffer16a = rand_buffer[chunkCounter];
			chunkCounter++;
			unsigned int buffer16b = rand_buffer[chunkCounter];
			chunkCounter++;

			if (buffer16a == 0) buffer16a = 1;
			if (buffer16b == 0) buffer16b = 1;

			//pad the parts, if needed
			while (buffer16a < 0x1000) {
				buffer16a = buffer16a * 0x10;
			}

			while (buffer16b < 0x1000) {
				buffer16b = buffer16b * 0x10;
			}

			unsigned int buffer32 = (buffer16a * 0x1000) + buffer16b;

			return buffer32;
		}

		unsigned int getChunk32() {

			unsigned int buffer32 = rand_buffer[chunkCounter];
			chunkCounter++;

			if (buffer32 == 0) buffer32 = 1;

			//pad the parts, if needed
			while (buffer32 < 0x10000000) {
				buffer32 = buffer32 * 0x10;
			}

			return buffer32;
		}

		int getPrefixIndex() {
			return prefix_index;
		}

		int getPrefixCount() {
			return prefixes.size();
		}

		Int getCurrentPrefix() {
			return prefixes[prefix_index];
		}

		Int getNextPrefix(bool rollover) {
			if (prefixes.size() == 0) return new Int((uint64_t)0);

			if (prefix_index >= prefixes.size()-1) {
				if (rollover) {
					prefix_index = 0;
				}
				else
				{
					return new Int((uint64_t)0);
				}
			}

			prefix_index++;
			return prefixes[prefix_index];
		}

		void resetPrefixIndex() {
			prefix_index = 0;
		}


		Int getCurrentIncrementor() {

			return incrementors[incrementor_index];
		}

		Int getNextIncrementor(bool rollover) {
			if (incrementors.size() == 0) return new Int((uint64_t)0);

			if (incrementor_index >= incrementors.size()-1) {
				if (rollover) {
					incrementor_index = 0;
				}
				else {
					return new Int((uint64_t)0);
				}
			}

			incrementor_index++;
			return incrementors[incrementor_index];
		}

		void resetIncrementorIndex() {
			incrementor_index = 1;
		}

		std::vector<Int> getRandomPool(Int min, Int max, uint64_t length) {

			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;
			//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]


			Int sixteen = new Int();
			sixteen.SetInt64(16);

			
			//uint64_t REPORTING_SIZE = length / 8;
			uint64_t generatedKeys = 0;

			uint64_t randomizerWidth = max.DeriveRandomizerWidth();

			Int range = max;
			range.Sub(&min);
			std::vector<Int> results;

			int bl = range.GetBitLength();
			unsigned char targetByteSize = (bl + 31) / 32;

			/* for wtf purposes
			std::string rStr = range.GetBase16();
			std::string minStr = min.GetBase16();
			std::string maxStr = max.GetBase16();
			*/
			

			while (generatedKeys < length) {

				Int result = Int((int64_t)0);

				for (int i = 0; i < randomizerWidth; i++) {
					if (targetByteSize > i) {

						result.SetDWord(i, getChunk(i, generatedKeys));


						if (targetByteSize > i && targetByteSize <= i + 1 && range.bits[i] != 0 && result.bits[i] > range.bits[i]) {
							result.bits[i] = result.bits[i] % range.bits[i];
						}

					}

				}
				result.Add(&min);

				/*
				//zero out last 4
				for (int i = 0; i < 4; i++) {
					result.Div(&sixteen);
				}

				for (int i = 0; i < 4; i++) {
					result.Mult(&sixteen);
				}
				*/

				results.push_back(result);
				generatedKeys++;
			}

			return results;
		}

		std::vector<Int> getRandomPool32(Int min, Int max, uint64_t length) {

			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;
			//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
			//- so that randomizers doesn't necessarily have to match len() - handy for experimentation

			//max = max.mul(16);
			//uint64_t REPORTING_SIZE = length / 8;
			uint64_t generatedKeys = 0;

			Int range = max;
			range.Sub(&min);
			std::vector<Int> results;

			int bl = range.GetBitLength();
			unsigned char targetByteSize = (bl + 31) / 32;

			/* for wtf purposes */
			std::string rStr = range.GetBase16();
			std::string minStr = min.GetBase16();
			std::string maxStr = max.GetBase16();


			while (generatedKeys < length) {

				Int result = Int((int64_t)0);

				for (int i = 0; i < 10; i++) {
					if (targetByteSize > i) {

						result.SetDWord(i, getChunk32());

						if (targetByteSize > i && targetByteSize <= i + 1 && range.bits[i] != 0 && result.bits[i] > range.bits[i]) {
							result.bits[i] = result.bits[i] % range.bits[i];
						}

					}

				}
				result.Add(&min);
				results.push_back(result);
				generatedKeys++;
			}

			return results;
		}

		std::vector<Int> getMaskedRandomPool(Int min, Int max, uint64_t length) {

			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;
			//this sort of assumes the min and max are the same bit length


			//Samples and scaling: 
			uint64_t keyLen = min.GetBase16Length();

			Int prefixSample = prefixes[0];
			uint64_t pfxLen = prefixSample.GetBase16Length();

			uint64_t generatedKeys = 0;

			Int range = max;
			range.Sub(&min);
			std::vector<Int> results;

			int bl = range.GetBitLength();
			unsigned char targetByteSize = (bl + 31) / 32;


			while (generatedKeys < length) {

				Int prefixResult = getNextPrefix(true);

				prefixResult.ExpandRight(keyLen - pfxLen);
				Int randResult = Int((int64_t)0);

				for (int i = 0; i < 10; i++) {
					if (targetByteSize > i) {

						randResult.SetDWord(i, getChunk(i, generatedKeys));


						if (targetByteSize > i && targetByteSize <= i + 1 && range.bits[i] != 0 && randResult.bits[i] > range.bits[i]) {
							randResult.bits[i] = randResult.bits[i] % range.bits[i];
						}

					}

				}

				//add back to min to place within range
				randResult.Add(&min);

				//randResult.ZeroRight(4);
				randResult.ZeroRight(6);

				randResult.ZeroLeft(pfxLen);

				prefixResult.Add(&randResult);
				/*
				std::string sanityCheck = prefixResult.GetBase16().substr(0, 3);

				if (sanityCheck == "F70") {// prefixResult.GetBase16().size() < sMin.size()) {
					//wtf check
					int i = 69;
				}
				*/
				results.push_back(prefixResult);
				generatedKeys++;
			}

			return results;
		}


		std::vector<Int> getMaskedRandomPool(Int prefix, Int min, Int max, uint64_t length) {

			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;
			//this sort of assumes the min and max are the same bit length


			//Samples and scaling: 
			uint64_t keyLen = min.GetBase16Length();
			uint64_t pfxLen = prefix.GetBase16Length();

			uint64_t generatedKeys = 0;

			Int range = max;
			range.Sub(&min);
			std::vector<Int> results;

			int bl = range.GetBitLength();
			unsigned char targetByteSize = (bl + 31) / 32;


			while (generatedKeys < length) {

				Int prefixResult = prefix;

				prefixResult.ExpandRight(keyLen - pfxLen);
				Int randResult = Int((int64_t)0);

				for (int i = 0; i < 10; i++) {
					if (targetByteSize > i) {

						randResult.SetDWord(i, getChunk(i, generatedKeys));


						if (targetByteSize > i && targetByteSize <= i + 1 && range.bits[i] != 0 && randResult.bits[i] > range.bits[i]) {
							randResult.bits[i] = randResult.bits[i] % range.bits[i];
						}

					}

				}

				//add back to min to place within range
				randResult.Add(&min);

				randResult.ZeroRight(3);
				randResult.ZeroLeft(pfxLen);

				prefixResult.Add(&randResult);

				
				/*
				std::string sanityCheck = prefixResult.GetBase16().substr(0, 3);

				if (sanityCheck == "F70") {// prefixResult.GetBase16().size() < sMin.size()) {
					//wtf check
					int i = 69;
				}
				*/
				results.push_back(prefixResult);


				generatedKeys++;
			}

			return results;
		}


		std::vector<Int> getMaskedRandomPoolV1(Int min, Int max, uint64_t length) {

			//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
			//randomizers are a [col][row] vector of random values from 0 to 0xffff;
			//this sort of assumes the min and max are the same bit length

			Int sixteen = new Int();
			sixteen.SetInt64(16);

			//Samples and scaling: 

			Int prefixSample = prefixes[0];

			uint64_t scaler = prefixSample.GetBase16().size();
			
			std::string sMin = min.GetBase16();
			std::string sMax = max.GetBase16();

			int strLength = sMin.size();
			int bLen = min.GetBitLength();
			int sz = min.GetSize();
			int sz64 = min.GetSize64();

			std::string newMin = sMin.substr(scaler, sMin.size() - scaler);
			std::string newMax = sMax.substr(scaler, sMax.size() - scaler);

			if (newMin.substr(0, 1) == "0") newMin[0] = '1';
			if (newMax.substr(0, 1) == "0") newMax[0] = 'F';

			uint64_t baseScale = newMin.size();

			min.SetBase16(newMin.c_str());
			max.SetBase16(newMax.c_str());
			uint64_t generatedKeys = 0;


			Int range = max;
			range.Sub(&min);
			std::vector<Int> results;

			int bl = range.GetBitLength();
			unsigned char targetByteSize = (bl + 31) / 32;

			/* for wtf purposes
			std::string rStr = range.GetBase16();
			std::string minStr = min.GetBase16();
			std::string maxStr = max.GetBase16();
			*/


			while (generatedKeys < length) {

				Int prefixResult = getNextPrefix(true);

				for (int i = 0; i < baseScale; i++)
				{
					prefixResult.Mult(&sixteen);
				}

				Int randResult = Int((int64_t)0);

				for (int i = 0; i < 10; i++) {
					if (targetByteSize > i) {

						randResult.SetDWord(i, getChunk(i, generatedKeys));


						if (targetByteSize > i && targetByteSize <= i + 1 && range.bits[i] != 0 && randResult.bits[i] > range.bits[i]) {
							randResult.bits[i] = randResult.bits[i] % range.bits[i];
						}

					}

				}

				//add back to min to place within range
				randResult.Add(&min);

				//zero out last 4
				for (int i = 0; i < 4; i++) {
					randResult.Div(&sixteen);
				}

				for (int i = 0; i < 4; i++) {
					randResult.Mult(&sixteen);
				}

				prefixResult.Add(&randResult);

				std::string sanityCheck = prefixResult.GetBase16().substr(0, 3);

				results.push_back(prefixResult);
				generatedKeys++;
			}

			return results;
		}

		~GPURand() {
			//delete rand_buffer;
		}
	};

}
