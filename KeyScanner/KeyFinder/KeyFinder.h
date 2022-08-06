#ifndef _KEY_FINDER_H
#define _KEY_FINDER_H

#include <stdint.h>
#include <vector>
#include <set>
#include <string>
#include "../Secp256k1/secp256k1.h"
#include "KeySearchTypes.h"
#include "../CudaDevice/CudaKeySearchDevice.h"
#include "KeySearchDevice.h"


class KeyFinder {

private:

	CudaKeySearchDevice* _device;

	unsigned int _compression;
	unsigned int _searchMode;

	std::set<KeySearchTargetHash160> _targetsHash160;

	uint64_t _statusInterval;

	secp256k1::uint256 _skip = 0;
	secp256k1::uint256 _stride = 1;
	std::string _keyMask;

	int _netSize = 0;
	int _randFill = 0;

	uint64_t _iterCount;
	uint64_t _totalIterationCount;

	uint64_t _total;
	uint64_t _totalTime;

	uint64_t _restartCount = 0;

	secp256k1::uint256 _startKey;
	secp256k1::uint256 _endKey;

	uint64_t _lastHour;
	uint64_t _totalHours;

	uint64_t _maxHours;
	uint64_t _maxSeconds;
	uint64_t _maxCycles;
	
	uint64_t _autoStrides;
	//uint64_t _strideQueueSize;
	//uint64_t _strideQueueIndex;

	//uint64_t _currentStrideIndex;
	//uint64_t _maxStrideIndex;

	secp256k1::StrideMapEntry _strideMapEntry;

	// Each index of each thread gets a flag to indicate if it found a valid hash
	bool _running;

	bool _randomStride;
	bool _continueAfterEnd;
	uint32_t _randomSrtrideBits;
	uint32_t _rStrideCount;
	std::vector<uint32_t> _rStrideHistory;

	//std::vector<secp256k1::StrideMapEntry> _strideQueue;

	void(*_resultCallback)(KeySearchResult);
	void(*_statusCallback)(KeySearchStatus);


	static void defaultResultCallback(KeySearchResult result);
	static void defaultStatusCallback(KeySearchStatus status);

	void removeTargetFromListHash160(const unsigned int value[5]);
	bool isTargetInListHash160(const unsigned int value[5]);
	//void setTargetsOnDevice();

	void reSetupEverything();

public:

	KeyFinder();

	KeyFinder(CudaKeySearchDevice*& cudaDvc, const secp256k1::uint256& startKey, const secp256k1::uint256& endKey, int compression, int searchMode,
	  const secp256k1::StrideMapEntry& stride, const std::string& key_mask, const secp256k1::uint256& skip, int netSize, int randFill, 
		uint64_t max_cycles, uint64_t auto_strides, uint64_t max_seconds, uint64_t max_hours, bool randomStride, bool continueAfterEnd,
		uint32_t randomSrtrideBits);

	~KeyFinder();

	void init(int deviceId, int threads, int pointsperThread, int blocks, int step, secp256k1::StrideMapEntry sme, secp256k1::KeyBuffers startingKeys);
	void run();
	void stop();

	secp256k1::KeyBuffers getKeyBuffers();

	void setResultCallback(void(*callback)(KeySearchResult));
	void setStatusCallback(void(*callback)(KeySearchStatus));
	void setStatusInterval(uint64_t interval);

	void setTargets(std::string targetFile);
	void setTargets(std::vector<std::string>& targets);

	secp256k1::uint256 getNextKey();


	static uint64_t LZC(uint64_t x);
	static uint64_t TZC(uint64_t x);
};

#endif