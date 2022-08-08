#ifndef _CUDA_KEY_SEARCH_DEVICE
#define _CUDA_KEY_SEARCH_DEVICE

#include "../KeyFinder/KeySearchDevice.h"
#include <vector>
#include <cuda_runtime.h>
#include "../Secp256k1/secp256k1.h"
#include "CudaDeviceKeys.h"
#include "CudaHashLookup.h"
#include "CudaAtomicList.h"
#include "../CudaUtil/cudaUtil.h"



// Structures that exist on both host and device side
struct CudaDeviceResult {
	int thread;
	int block;
	int idx;
	bool compressed;
	unsigned int x[8];
	unsigned int y[8];
	unsigned int digest[5];
};

class CudaKeySearchDevice {

private:

	int _device;

	int _blocks;

	int _threads;

	int _pointsPerThread;

	int _compression;

	int _searchMode;

	std::vector<KeySearchResult> _results;

	std::string _deviceName;

	secp256k1::uint256 _startExponent;
	secp256k1::uint256 _endExponent;

	secp256k1::KeyBuffers _startingKeys;

	std::vector<secp256k1::KeyGenGuide> _keyParts;
	secp256k1::KeyScaffold _keyTemplate;
	

	uint64_t _iterations;

	void cudaCall(cudaError_t err);

	std::vector<secp256k1::uint256> getGPUAssistedRandoms(std::vector<unsigned int> bufferpool, std::vector<std::vector<unsigned int>> randomizers, std::vector<uint64_t>& bufferRowIndex, secp256k1::uint256 min, secp256k1::uint256 max, uint64_t length);

	secp256k1::KeyScaffold parseKeyMask(std::string keyMaskStr, uint64_t totalPoints);

	void CudaKeySearchDevice::generateKeyBuffers(secp256k1::KeyScaffold, uint64_t totalPoints);
	void CudaKeySearchDevice::assembleKeys();
	
	void generateStartingPoints();

	void showAssemblyBuffer(std::vector<std::vector<unsigned int>> assembly_buffer, bool showDetail);

	void reGenerateStartingPoints();

	void reUseStartingPoints();

	void restoreStartingPoints();

	void reDistributeStartingPoints(uint64_t divider, bool randomOnly, bool distance_based);

	CudaDeviceKeys* _deviceKeys;
	CudaAtomicList _resultList;
	CudaHashLookup _targetLookup;

	cuda::CudaDeviceInfo _info;

	void getResultsInternal(secp256k1::uint256 current_stride, int current_strideQueueIndex);

	std::vector<hash160> _targetsHash160;

	bool isTargetInListHash160(const unsigned int hash[5]);
	void removeTargetFromListHash160(const unsigned int hash[5]);

	GPUMatrix getPrivateKeyOffset(int thread, int block, int point);

	std::string _keyMask;

	secp256k1::uint256 _stride;
	secp256k1::uint256 _skip;

	uint64_t _strideQueueIndex;

	std::vector<secp256k1::StrideMapEntry> _strideQueue;


public:

	CudaKeySearchDevice();
	CudaKeySearchDevice(int device, int threads, int pointsPerThread, int blocks = 0);
	~CudaKeySearchDevice();

	void init(const secp256k1::uint256& start, const secp256k1::uint256& end, int compression, int searchMode, int step, const secp256k1::StrideMapEntry& stride, const std::string& keyMask, secp256k1::KeyBuffers& startingKeys, const secp256k1::uint256& skip);

	secp256k1::KeyBuffers getKeyBuffers();
	
	void doStep();

	void setTargets(const std::set<KeySearchTargetHash160>& targets);
	
	size_t getResults(std::vector<KeySearchResult>& results);

	uint64_t keysPerStep();

	std::string getDeviceName();

	void getMemoryInfo(uint64_t& freeMem, uint64_t& totalMem);

	secp256k1::uint256 getNextKey();

	// Update stride
	void updateStride(secp256k1::StrideMapEntry strideEntry, bool reset);

	secp256k1::StrideMapEntry nextStride();

	void resetCuda();

};

#endif