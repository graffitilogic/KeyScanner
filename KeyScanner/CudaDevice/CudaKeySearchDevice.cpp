#include "CudaKeySearchDevice.h"
#include "../Logger/Logger.h"
#include "../Util/util.h"
#include "cudabridge.h"
#include <sstream>
#include "../AddrUtil/AddressUtil.h"
#include "../mathstuff.h"
#include "../RandomHelper.h"
#include <chrono>


void CudaKeySearchDevice::cudaCall(cudaError_t err)
{
	if (err) {
		std::string errStr = cudaGetErrorString(err);
	
		Logger::log(LogLevel::Error, "[**CUDACALL ERR**] " + errStr);
		throw KeySearchException(errStr);
	}
}

CudaKeySearchDevice::CudaKeySearchDevice()
{
	_device = 0;
	_deviceKeys = 0;
	_blocks = 0;
	_compression = 0;
	_iterations = 0;
	_pointsPerThread = 0;
	_searchMode = 0;
	_strideQueueIndex = 0;
	_threads = 0;
}

CudaKeySearchDevice::CudaKeySearchDevice(int device, int threads, int pointsPerThread, int blocks)
{
	//cuda::CudaDeviceInfo info;
	try {
		_info = cuda::getDeviceInfo(device);
		_deviceName = _info.name;
	}
	catch (cuda::CudaException ex) {
		throw KeySearchException(ex.msg);
	}

	if (threads <= 0 || threads % 32 != 0) {
		throw KeySearchException("The number of threads must be a multiple of 32");
	}

	if (pointsPerThread <= 0) {
		throw KeySearchException("At least 1 point per thread required");
	}

	// Specifying blocks on the commandline is depcreated but still supported. If there is no value for
	// blocks, devide the threads evenly among the multi-processors
	if (blocks == 0) {
		if (threads % _info.mpCount != 0) {
			throw KeySearchException("The number of threads must be a multiple of " + util::format("%d", _info.mpCount));
		}

		_threads = threads / _info.mpCount;

		_blocks = _info.mpCount;

		while (_threads > 512) {
			_threads /= 2;
			_blocks *= 2;
		}
	}
	else {
		_threads = threads;
		_blocks = blocks;
	}

	_iterations = 0;

	_device = device;

	_pointsPerThread = pointsPerThread;
}

void CudaKeySearchDevice::init(const secp256k1::uint256& start, const secp256k1::uint256& end, int compression, int searchMode, int step, const secp256k1::StrideMapEntry& stride, const std::string& keyMask, secp256k1::KeyBuffers& startingKeys, const secp256k1::uint256& skip)
{
	if (start.cmp(secp256k1::N) >= 0) {
		throw KeySearchException("Starting key is out of range");
	}

	_startExponent = start;
	_endExponent = end;

	_compression = compression;

	_searchMode = searchMode;

	_stride = stride.stride;

	_skip = skip;

	_keyMask = keyMask;

	_startingKeys = startingKeys;
	_strideQueueIndex = stride.strideQueueIndex;

	//Logger::log(LogLevel::Debug, "KeyFinder Literals: " + key)

	if (step == 0) {

		cudaCall(cudaSetDevice(_device));
	
		//if you call thrust before this, the line below blows an error. Ignore for now.  TODO: Revisit thrust implications on device status.
		// Block on kernel calls
		cudaCall(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync));

		// Use a larger portion of shared memory for L1 cache
		cudaCall(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

	}

	//_deviceKeys = new CudaDeviceKeys();
	//Logger::log(LogLevel::Debug, "Directive: " + stride.directive + " Step: " + util::formatThousands(step));
	if (stride.directive == "reset") {
		//reGenerateStartingPoints();
		reUseStartingPoints();
	}
	else if (stride.directive == "restore") {
		restoreStartingPoints();
	}
	else if (stride.directive == "regenerate") {
		reGenerateStartingPoints();
	}
	else if (stride.directive == "redistribute") {
		reDistributeStartingPoints(stride.cycles, false, false);
	}
	else if (stride.directive == "redistribute-random") {
		reDistributeStartingPoints(stride.cycles, true, false);
	}
	else if (stride.directive == "redistribute-distance") {
		reDistributeStartingPoints(stride.cycles, false, true);
	}
	else {
		if (_startingKeys.Keys.size() == 0) {
			//Logger::log(LogLevel::Debug, "generateStartingPoints()");
			generateStartingPoints();
		}
		else {
			//Logger::log(LogLevel::Debug, "reUseStartingPoints()");
			reUseStartingPoints();
		}
	}

	//Logger::log(LogLevel::Debug, "allocateChainBuf(" + std::to_string(_threads) + "," + std::to_string(_blocks) + ","  + std::to_string(_pointsPerThread) + ")");
	cudaCall(allocateChainBuf(_threads * _blocks * _pointsPerThread));

	// Set the incrementor
	secp256k1::ecpoint g = secp256k1::G();
	secp256k1::ecpoint p = secp256k1::multiplyPoint(_stride, g);   //1*_stride instead of matrix

	//in random scenarios, the points aren't related / sequential, so the incrementor below doesn't apply to this.
	//secp256k1::ecpoint p = secp256k1::multiplyPoint(secp256k1::uint256((uint64_t)_threads * _blocks * _pointsPerThread) * _stride, g);
	cudaCall(_resultList.init(sizeof(CudaDeviceResult), 16));

	cudaCall(setIncrementorPoint(p.x, p.y));
}


secp256k1::KeyBuffers CudaKeySearchDevice::getKeyBuffers() {
	return _startingKeys;
 }

CudaKeySearchDevice::~CudaKeySearchDevice()
{
	delete _deviceKeys;
	_deviceKeys = NULL;
}

std::vector<secp256k1::uint256> CudaKeySearchDevice::getGPUAssistedRandoms(std::vector<unsigned int> bufferpool, std::vector<std::vector<unsigned int>> randomizers, std::vector<uint64_t>& bufferRowIndex, secp256k1::uint256 min, secp256k1::uint256 max, uint64_t length) {
	//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
	//randomizers are a [col][row] vector of random values from 0 to 0xffff;
	//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
	//- so that randomizers doesn't necessarily have to match len() - handy for experimentation

	secp256k1::uint256 range = max.sub(min);
	std::vector<secp256k1::uint256> results;

	int col_max = bufferpool.size();
	uint64_t block_size = 65536;

	//uint64_t bufferIndex = 1;

	unsigned char targetByteSize = (range.getBitRange() + 31) / 32;

	while (results.size() < length) {

		secp256k1::uint256 result;

		for (int i = 0; i < 8; i++) {

			if (targetByteSize > i) {

				//int bColOffset = i + 8;
				int rowOffset = bufferRowIndex[i];
				int randomizerRow = randomizers[i][bufferRowIndex[i]];
				int randomizerRowAlt = randomizers[i + 8][bufferRowIndex[i + 8]];

				unsigned int partA = bufferpool[randomizerRow];
				unsigned int partB = bufferpool[randomizerRowAlt];

				result.v[i] = (partA * block_size) + partB;  // combine the two parts - I was thinking of doing an AB/BA tiktok here but ...that doesn't really do what I thought.

				if (targetByteSize > i && targetByteSize <= i + 1 && range.v[i] != 0 && result.v[i] > range.v[i]) {
					result.v[i] %= range.v[i];
				}

			}


			bufferRowIndex[i]++;  //increment row index for this column
			if (bufferRowIndex[i] >= randomizers[i].size()) bufferRowIndex[i] = 0;  //rollover if needed.

			bufferRowIndex[i + 8]++;  //increment row index for this column
			if (bufferRowIndex[i + 8] >= randomizers[i + 8].size()) bufferRowIndex[i + 8] = 0;  //rollover if needed.
			//if (bufferIndex >= bufferpool[0].size()) bufferIndex = 1;  //rollover if needed.

		}


		//bufferIndex++;
		//if (results.size() % 2000 == 0) Logger::log(LogLevel::Info, "A:" + result.toString() + " B:" + result.reverse_shallow().toString());

		results.push_back(result);
		results.push_back(result.reverse());
	}

	return results;
}

secp256k1::KeyScaffold CudaKeySearchDevice::parseKeyMask(std::string keyMaskStr, uint64_t totalPoints) {

	secp256k1::KeyScaffold result;

	result.KeyMask = keyMaskStr;

	std::stringstream ss;

	//build out literal mask -  turns 8100rrrrrrrrnnnn into 81000000000000000000
	for (int c = 0; c < keyMaskStr.size();c++) {
		if (keyMaskStr[c] != 'r' && keyMaskStr[c] != 'n') {
			ss << keyMaskStr[c];
		}
		else {
			ss << '0';
		}
	}
	result.LiteralMask = ss.str();
	ss.str("");
	ss.clear();

	//build out sequential mask  - turns 8100rrrrrrrrnnnn into 000000000000nnnn
	for (int c = 0; c < keyMaskStr.size();c++) {
		if (keyMaskStr[c] == 'n') {
			ss << keyMaskStr[c];
		}
		else {
			ss << '0';
		}
	}
	result.SequentialMask = ss.str();
	ss.str("");
	ss.clear();

	//build out the random mask  - turns 8100rrrrrrrrnnnn into 0000rrrrrrrr0000
	for (int c = 0; c < keyMaskStr.size();c++) {
		if (keyMaskStr[c] == 'r') {
			ss << keyMaskStr[c];
		}
		else {
			ss << '0';
		}
	}
	result.RandomMask = ss.str();
	ss.str("");
	ss.clear();


	return result;
}

void CudaKeySearchDevice::generateKeyBuffers(secp256k1::KeyScaffold bufferTemplate, uint64_t totalPoints) {
	uint64_t bufferSizes = totalPoints / 2;
	uint64_t startMs;
	uint64_t endMs;
	float elapsedMs;

	startMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//process literal wave
	secp256k1::uint256 literalBase = secp256k1::uint256(bufferTemplate.LiteralMask);


	std::vector<secp256k1::uint256> literals;
	for (int i = 0; i < totalPoints; i++) {
		literals.push_back(literalBase);
	}

	std::vector<secp256k1::uint256>().swap(_startingKeys.Literals);
	_startingKeys.Literals = literals;

	std::vector<secp256k1::uint256>().swap(_startingKeys.Keys);
	_startingKeys.Keys = literals;

	std::vector<secp256k1::uint256>().swap(literals);

	endMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	elapsedMs = endMs - startMs;
	Logger::log(LogLevel::Info, util::formatThousands(_startingKeys.Literals.size()) + " literal entries generated in " + util::formatThousands(elapsedMs) + "ms");

	bool containsSequential = (bufferTemplate.SequentialMask.find_first_of('n') != std::string::npos);

	//reusable randomizers
	if (containsSequential) {

		startMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

		//process sequential wave
		std::vector<std::vector<secp256k1::uint256>> sequentialPools;

		Logger::log(LogLevel::Info, "Generating Sequential Pools..");
		int sequentialCount = 0;
		for (int c = 0; c < bufferTemplate.SequentialMask.size(); c++) {
			if (bufferTemplate.SequentialMask[c] == 'n') sequentialCount++;
			if (bufferTemplate.SequentialMask[c] != 'n' && sequentialCount > 0) {
				//fill a sequential buffer of [sequential count] length
				secp256k1::uint256 startRange = secp256k1::getRangeStart(sequentialCount, "1");
				secp256k1::uint256 endRange = secp256k1::getRangeEnd(sequentialCount, "F");

				Logger::log(LogLevel::Info, "Getting Sequentials from " + startRange.toString() + " - " + endRange.toString());
				std::vector<secp256k1::uint256> tmpSequentials = secp256k1::getSequentialRange(startRange, endRange, false);
				sequentialPools.push_back(tmpSequentials);
				
				std::vector<secp256k1::uint256>().swap(tmpSequentials);

				Logger::log(LogLevel::Info, "Added Sequential Pool of " + util::formatThousands(tmpSequentials.size()));
				sequentialCount = 0;
			}
		}

		Logger::log(LogLevel::Info, "Flushing Final Sequential Pool..");
		//any sequentials left to generate?
		if (sequentialCount > 0) {
			//fill a sequential buffer of [sequential count] length
			secp256k1::uint256 startRange = secp256k1::getRangeStart(sequentialCount, "1");
			secp256k1::uint256 endRange = secp256k1::getRangeEnd(sequentialCount, "F");
			std::vector<secp256k1::uint256> tmpSequentials = secp256k1::getSequentialRange(startRange, endRange, false);
			sequentialPools.push_back(tmpSequentials);
			
			std::vector<secp256k1::uint256>().swap(tmpSequentials);
		}

		Logger::log(LogLevel::Info, "Building Sequential Randomizers..");
		std::vector<std::vector<uint64_t>> sequentialRandomizers;

		std::vector<uint64_t> sequentialIndexes;
		for (int s = 0; s < sequentialPools.size(); s++) {
			uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			timeSeed = timeSeed * (s + 1);
			uint64_t sz = sequentialPools[s].size();
			uint64_t startRndRange = 0;
			uint64_t endRndRange = sequentialPools[s][sz - 1].toInt64() -1; //todo:research implications of 1 start vs 0 start and skipped keys
			std::vector<uint64_t> results = RandomHelper::getRndRange(timeSeed, startRndRange, endRndRange, sz - 1);
			sequentialRandomizers.push_back(results);
			sequentialIndexes.push_back(0);
		}

		Logger::log(LogLevel::Info, "Generating Sequential Keys..");
		std::vector<secp256k1::uint256> sequentialKeys;

		while (sequentialKeys.size() < totalPoints) {
			int sequentialBlock = 0;

			//seed the key
			uint64_t poolIndex = sequentialRandomizers[sequentialBlock][sequentialIndexes[sequentialBlock]];
			secp256k1::uint256 thisKey = sequentialPools[sequentialBlock][poolIndex];
			sequentialIndexes[sequentialBlock]++;
			if (sequentialIndexes[sequentialBlock] == sequentialRandomizers[sequentialBlock].size() - 1) sequentialIndexes[sequentialBlock] = 0;

			int nCount = 0;

			//step through the sequential mask and construct the sequential key.
			for (int c = 0; c < bufferTemplate.SequentialMask.size(); c++) {
				if (sequentialBlock > 0) thisKey = thisKey.mul(16);
				if (bufferTemplate.SequentialMask[c] == 'n') nCount++;
				if (bufferTemplate.SequentialMask[c] != 'n' && nCount > 0) {
					if (sequentialBlock > 0) {
						poolIndex = sequentialRandomizers[sequentialBlock][sequentialIndexes[sequentialBlock]];
						secp256k1::uint256 thisKeyPart = sequentialPools[sequentialBlock][poolIndex];
						thisKey = thisKey.add(thisKeyPart);

						sequentialIndexes[sequentialBlock]++;
						if (sequentialIndexes[sequentialBlock] == sequentialRandomizers[sequentialBlock].size() - 1) sequentialIndexes[sequentialBlock] = 0;
					}
					sequentialBlock++;
					nCount = 0;
				}
			}

			thisKey = thisKey.mul(16);

			//pick up the remainder
			if (nCount > 0) {
				uint64_t poolIndex = sequentialRandomizers[sequentialBlock][sequentialIndexes[sequentialBlock]];
				secp256k1::uint256 thisKeyPart = sequentialPools[sequentialBlock][poolIndex];
				thisKey = thisKey.add(thisKeyPart);

				sequentialIndexes[sequentialBlock]++;
				if (sequentialIndexes[sequentialBlock] == sequentialRandomizers[sequentialBlock].size() - 1) sequentialIndexes[sequentialBlock] = 0;
			}

			sequentialKeys.push_back(thisKey);
		}

		std::vector<uint64_t>().swap(sequentialIndexes);
		std::vector<std::vector<uint64_t>>().swap(sequentialRandomizers);
		std::vector<std::vector<secp256k1::uint256>>().swap(sequentialPools);
		std::vector<secp256k1::uint256>().swap(_startingKeys.Sequentials);

		_startingKeys.Sequentials = sequentialKeys;

		std::vector<secp256k1::uint256>().swap(sequentialKeys);

		endMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		elapsedMs = endMs - startMs;
		Logger::log(LogLevel::Info, util::formatThousands(_startingKeys.Sequentials.size()) + " sequential entries generated in " + util::formatThousands(elapsedMs) + "ms");

	}

	//at this point we have some buffers of sequential numbers and some randomizers for each buffer.   
	//we then combine this all into the "seqential-portion" of the key.   we could do it sequentially or we could do it random-assistened from sequential pools.

	startMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//process random wave
	bool containsRandom = (bufferTemplate.RandomMask.find_first_of('r') != std::string::npos);


	//reusable randomizers
	if (containsRandom) {

		Logger::log(LogLevel::Info, "Building Randomizers..");
		uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

		std::vector<std::vector<unsigned int>>().swap(_startingKeys.Randomizers);
		//_startingKeys.Randomizers.clear();
		_startingKeys.Randomizers = RandomHelper::getRandomizers(timeSeed, bufferSizes, 16);

		std::vector<uint64_t> bufferRowIndex;

		while (bufferRowIndex.size() <= bufferSizes) {
			bufferRowIndex.push_back(0);
		}

		std::vector<unsigned int> static_assembly_buffer = RandomHelper::getStaticAssemblyBuffer();
		std::vector<std::vector<secp256k1::uint256>> randomPools;

		Logger::log(LogLevel::Info, "Generating Random Keys..");

		int randomCount = 0;
		for (int c = 0; c < bufferTemplate.RandomMask.size(); c++) {
			if (bufferTemplate.RandomMask[c] == 'r') randomCount++;
			if (bufferTemplate.RandomMask[c] != 'r' && randomCount > 0) {
				//fill a random buffer of [rnd count] length
				secp256k1::uint256 startRange = secp256k1::getRangeStart(randomCount, "1");
				secp256k1::uint256 endRange = secp256k1::getRangeEnd(randomCount, "F");
				std::vector<secp256k1::uint256> tmpRandoms = getGPUAssistedRandoms(static_assembly_buffer, _startingKeys.Randomizers, bufferRowIndex, startRange, endRange, totalPoints);
				randomPools.push_back(tmpRandoms);

				std::vector<secp256k1::uint256>().swap(tmpRandoms);
				randomCount = 0;
			}
		}

		//any randoms left to generate?
		if (randomCount > 0) {
			//fill a random buffer of [rnd count] length
			secp256k1::uint256 startRange = secp256k1::getRangeStart(randomCount, "1");
			secp256k1::uint256 endRange = secp256k1::getRangeEnd(randomCount, "F");
			std::vector<secp256k1::uint256> tmpRandoms = getGPUAssistedRandoms(static_assembly_buffer, _startingKeys.Randomizers, bufferRowIndex, startRange, endRange, totalPoints);
			randomPools.push_back(tmpRandoms);
			std::vector<secp256k1::uint256>().swap(tmpRandoms);
		}


		std::vector<secp256k1::uint256> randomKeys;
		std::vector<uint64_t> randomIndexes;
		for (int r = 0; r < randomPools.size(); r++) {
			randomIndexes.push_back(0);
		}


		while (randomKeys.size() < totalPoints) {
			int randomBlock = 0;

			secp256k1::uint256 thisKey;
			int rCount = 0;

			//step through the sequential mask and construct the sequential key.
			for (int c = 0; c < bufferTemplate.RandomMask.size(); c++) {

				thisKey = thisKey.mul(16);

				if (bufferTemplate.RandomMask[c] == 'r') rCount++;
				if (bufferTemplate.RandomMask[c] != 'r' && rCount > 0) {
					uint64_t poolIndex = _startingKeys.Randomizers[randomBlock][randomIndexes[randomBlock]];
					secp256k1::uint256 thisKeyPart = randomPools[randomBlock][poolIndex];
					thisKey = thisKey.add(thisKeyPart);

					randomIndexes[randomBlock]++;
					if (randomIndexes[randomBlock] == _startingKeys.Randomizers[randomBlock].size() - 1) randomIndexes[randomBlock] = 0;
					randomBlock++;
					rCount = 0;
				}
			}

			thisKey = thisKey.mul(16);

			//pick up the remainder
			if (rCount > 0) {
				uint64_t poolIndex = _startingKeys.Randomizers[randomBlock][randomIndexes[randomBlock]];
				secp256k1::uint256 thisKeyPart = randomPools[randomBlock][poolIndex];
				thisKey = thisKey.add(thisKeyPart);

				randomIndexes[randomBlock]++;
				if (randomIndexes[randomBlock] == _startingKeys.Randomizers[randomBlock].size() - 1) randomIndexes[randomBlock] = 0;
			}

			randomKeys.push_back(thisKey);
		}

		std::vector<std::vector<secp256k1::uint256>>().swap(randomPools);
		std::vector<uint64_t>().swap(randomIndexes);
		std::vector<secp256k1::uint256>().swap(_startingKeys.Randoms);

		_startingKeys.Randoms = randomKeys;

		std::vector<secp256k1::uint256>().swap(randomKeys);

		endMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		elapsedMs = endMs - startMs;
		Logger::log(LogLevel::Info, util::formatThousands(_startingKeys.Randoms.size()) + " random entries generated in " + util::formatThousands(elapsedMs) + "ms");
	}
}

void CudaKeySearchDevice::assembleKeys() {

	uint64_t REPORTING_SIZE = _startingKeys.Keys.size() / 8;
	uint64_t generatedKeys = 0;

	std::vector<secp256k1::uint256>().swap(_startingKeys.BetaKeys);

	uint64_t startMs;
	uint64_t endMs;
	float elapsedMs;

	startMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	std::vector<uint64_t> randomizers = RandomHelper::getRndRange(timeSeed, 0, _startingKeys.Randoms.size() - 1, _startingKeys.Keys.size());

	endMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	elapsedMs = endMs - startMs;

	Logger::log(LogLevel::Info, util::formatThousands(_startingKeys.Literals.size()) + " randomizers generated in " + util::formatThousands(elapsedMs) + "ms");
	Logger::log(LogLevel::Info, "Assembling Keys..");

	startMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	for (int k = 0; k < _startingKeys.Keys.size(); k++) {
		secp256k1::uint256 thisKey = _startingKeys.Keys[k];
		//Logger::log(LogLevel::Debug, "Base Key: " + thisKey.toString());

		thisKey = thisKey.add(_startingKeys.Sequentials[k]);
		//Logger::log(LogLevel::Debug, "Seqential Pass: [" + _startingKeys.Sequentials[k].toString() + "] " +  thisKey.toString());
		_startingKeys.BetaKeys.push_back(thisKey);   //pre-random version of the key.

		thisKey = thisKey.add(_startingKeys.Randoms[randomizers[k]]);
		//Logger::log(LogLevel::Debug, "Random Pass: [" + _startingKeys.Randoms[randomizers[k]].toString() +  "] " + thisKey.toString());
		_startingKeys.Keys[k] = thisKey;

		if (generatedKeys % REPORTING_SIZE == 0)  Logger::log(LogLevel::Info, "SAMPLE Key: " + thisKey.toString() + " (" + util::formatThousands(generatedKeys) + ")");
		generatedKeys++;

	}

	std::vector<uint64_t>().swap(randomizers);  //sweap instead of clear b/c of sloppy memory stuff.
	std::vector<secp256k1::uint256>().swap(_startingKeys.RootKeys);
	_startingKeys.RootKeys = _startingKeys.Keys;

	endMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	elapsedMs = endMs - startMs;
	Logger::log(LogLevel::Info, util::formatThousands(_startingKeys.Literals.size()) + " keys generated in " + util::formatThousands(elapsedMs) + "ms");

}

void CudaKeySearchDevice::generateStartingPoints()
{
	_deviceKeys = new CudaDeviceKeys();

	uint64_t RND_POOL_MULTIPLIER = 1;

	uint64_t block_size = 65536;
	uint64_t totalPoints = (uint64_t)_pointsPerThread * _threads * _blocks;
	uint64_t totalMemory = totalPoints * 40;

	Logger::log(LogLevel::Info, "NViDiA Compute Capability: " + util::formatThousands(_info.mpCount) + " (" + util::formatThousands(_info.cores) + " cores)");
	Logger::log(LogLevel::Info, "Concurrent Key Capacity: " + util::formatThousands(totalPoints) + "  (" + util::format("%.1f", (double)totalMemory / (double)(1024 * 1024)) + "MB)");
	Logger::log(LogLevel::Info, "Start Key: " + _startExponent.toString());
	Logger::log(LogLevel::Info, "End Key: " + _endExponent.toString());

	//Logger::log(LogLevel::Info, "Starting Key Generation (CPU-Bound): " + _endExponent.toString());
	if (_strideQueue.size() > 0) Logger::log(LogLevel::Info, "Stride Queue Size: " + util::formatThousands(_strideQueue.size()));

	secp256k1::uint256 endKey = _endExponent;
	secp256k1::uint256 keySpace = _endExponent.sub(_startExponent);

	//string representations of the start and end range keys
	std::string sStartKey = _startExponent.toString();
	std::string sEndKey = _endExponent.toString();

	if (!_keyMask.empty()) {
		_keyTemplate = parseKeyMask(_keyMask, totalPoints);
	}

	generateKeyBuffers(_keyTemplate, totalPoints);
	assembleKeys();

	if (true) { //todo: optionalize this
		uint64_t REPORTING_SIZE = _startingKeys.Keys.size() / 8;
		uint64_t generatedDistances = 0;

		Logger::log(LogLevel::Info, "Sorting keys.. ");
		std::vector<secp256k1::uint256> sortedKeys = RandomHelper::sortKeys(_startingKeys.Keys);
		_startingKeys.Distances = RandomHelper::getDistances(sortedKeys);

		Logger::log(LogLevel::Info, "Sorting Distances.. ");
		_startingKeys.Distances = RandomHelper::sortKeys(_startingKeys.Distances);

		//set the zeroeth distance against the starting key
		_startingKeys.Distances[0] = _startingKeys.Keys[0] - _startExponent;

		for (int k = 0; k < _startingKeys.Distances.size(); k++) {

			if (generatedDistances % REPORTING_SIZE == 0)  Logger::log(LogLevel::Info, "SAMPLE Distance " + _startingKeys.Distances[k].toString() + " (" + util::formatThousands(generatedDistances) + ")");
			generatedDistances++;
		}
	}


	//exponents = _startingKeys.Keys;
	Logger::log(LogLevel::Info, "Key Buffer Size: " + util::formatThousands(_startingKeys.Keys.size()));

	//Logger::log(LogLevel::Info, "");

	Logger::log(LogLevel::Info, "Writing " + util::formatThousands(_startingKeys.Keys.size()) + " keys to " + _info.name + "...");
	cudaCall(_deviceKeys->init(_blocks, _threads, _pointsPerThread, _startingKeys.Keys));

	// Show progress in 10% increments
	double pct = 10.0;
	for (int i = 1; i <= 256; i++) {
		cudaCall(_deviceKeys->doStep());

		if (((double)i / 256.0) * 100.0 >= pct) {
			if (pct <= 10.0)
				Logger::log2(LogLevel::Info, util::format("%.1f%%", pct));
			else
				printf("  %.1f%%", pct);
			pct += 10.0;
		}
	}

	printf("\n");
	Logger::log(LogLevel::Info, "Done");

	//_deviceKeys.clearPrivateKeys();
}

void CudaKeySearchDevice::showAssemblyBuffer(std::vector<std::vector<unsigned int>> assembly_buffer, bool showDetails) {
	//check out that buffer - copy this back out to see it
	for (int i = 0; i < assembly_buffer.size(); i++) {
		Logger::log(LogLevel::Info, "[" + std::to_string(i) + "] " + util::formatThousands(assembly_buffer[i].size()));
	}

	if (showDetails) {
		for (int i = 0; i < assembly_buffer[0].size(); i++) {
			std::stringstream ss;
			ss << std::to_string(i);
			ss << ">  ";
			for (int j = 0; j < 16;j++) {
				ss << std::to_string(assembly_buffer[j][i]);
				ss << " | ";
			}
			ss << "\n";

			Logger::log(LogLevel::Info, ss.str());
		}
	}

}

void CudaKeySearchDevice::reGenerateStartingPoints()
{
	Logger::log(LogLevel::Info, "Re-generating Starting Points.. " + util::formatThousands(_startingKeys.Keys.size()));
	_deviceKeys = new CudaDeviceKeys();

	//Load up the literals as the starting point for the keys
	std::vector<secp256k1::uint256>().swap(_startingKeys.Keys);
	_startingKeys.Keys = _startingKeys.Literals;


	//assembleKeys will use the literals as a starting point and repopulate the _startingKeys.keys vector with a new randomized set.
	assembleKeys();

	Logger::log(LogLevel::Info, "Key Buffer Size: " + util::formatThousands(_startingKeys.Keys.size()));
	Logger::log(LogLevel::Info, "Writing " + util::formatThousands(_startingKeys.Keys.size()) + " keys to " + _info.name + "...");
	
	cudaCall(_deviceKeys->init(_blocks, _threads, _pointsPerThread, _startingKeys.Keys));


	// Show progress in 10% increments
	double pct = 10.0;
	for (int i = 1; i <= 256; i++) {
		cudaCall(_deviceKeys->doStep());

		if (((double)i / 256.0) * 100.0 >= pct) {
			if (pct <= 10.0)
				Logger::log2(LogLevel::Info, util::format("%.1f%%", pct));
			else
				printf("  %.1f%%", pct);
			pct += 10.0;
		}
	}
	printf("\n");

	Logger::log(LogLevel::Info, "Done");
}

void CudaKeySearchDevice::reUseStartingPoints()
{
	_deviceKeys = new CudaDeviceKeys();

	//for random stride function, incrementing repeatable searches
	uint64_t totalPoints = (uint64_t)_pointsPerThread * _threads * _blocks;
	uint64_t totalMemory = totalPoints * 40;


	Logger::log(LogLevel::Info, "Writing " + util::formatThousands(_startingKeys.Keys.size()) + " keys back to " + _info.name + " for stride " + _stride.toString() + "...");

	cudaCall(_deviceKeys->init(_blocks, _threads, _pointsPerThread, _startingKeys.Keys));

	// Show progress in 10% increments
	double pct = 10.0;
	for (int i = 1; i <= 256; i++) {
		cudaCall(_deviceKeys->doStep());

		if (((double)i / 256.0) * 100.0 >= pct) {
			if (pct <= 10.0)
				Logger::log2(LogLevel::Info, util::format("%.1f%%", pct));
			else
				printf("  %.1f%%", pct);
			pct += 10.0;
		}
	}
	printf("\n");

	Logger::log(LogLevel::Info, "Done");
}

void CudaKeySearchDevice::restoreStartingPoints()
{
	_deviceKeys = new CudaDeviceKeys();

	//Load up the root starting points
	std::vector<secp256k1::uint256>().swap(_startingKeys.Keys);
	_startingKeys.Keys = _startingKeys.RootKeys;

	Logger::log(LogLevel::Info, "Restoring " + util::formatThousands(_startingKeys.Keys.size()) + " keys back to " + _info.name + " for stride " + _stride.toString() + "...");

	cudaCall(_deviceKeys->init(_blocks, _threads, _pointsPerThread, _startingKeys.Keys));


	// Show progress in 10% increments
	double pct = 10.0;
	for (int i = 1; i <= 256; i++) {
		cudaCall(_deviceKeys->doStep());

		if (((double)i / 256.0) * 100.0 >= pct) {
			if (pct <= 10.0)
				Logger::log2(LogLevel::Info, util::format("%.1f%%", pct));
			else
				printf("  %.1f%%", pct);
			pct += 10.0;
		}
	}
	printf("\n");

	Logger::log(LogLevel::Info, "Done");
}

void CudaKeySearchDevice::reDistributeStartingPoints(uint64_t divider, bool randomOnly, bool distance_based)
{
	Logger::log(LogLevel::Info, "Re-distributing Starting Points.. " + util::formatThousands(_startingKeys.Keys.size()));
	uint64_t REPORTING_SIZE = _startingKeys.Keys.size() / 8;
	uint64_t shiftedKeys = 0;

	_deviceKeys = new CudaDeviceKeys();

	//Load up the root starting points
	std::vector<secp256k1::uint256>().swap(_startingKeys.Keys);
	_startingKeys.Keys = _startingKeys.RootKeys;

	secp256k1::uint256 keySpace; 
	secp256k1::uint256 evenDistribution; 

	if (randomOnly) {
		secp256k1::BetaKeyManipulator bkm = secp256k1::parseBetaKey(_startingKeys.BetaKeys[0], '0');
		keySpace = bkm.keyB.sub(bkm.keyA);
		evenDistribution = keySpace.div(_startingKeys.Keys.size());
	
		if (evenDistribution == 0) { //keyspace < totalkeys
			uint64_t keySpaceExpander = _startingKeys.Keys.size() / keySpace.toUint64();
			evenDistribution = keySpace.mul(keySpaceExpander).div(_startingKeys.Keys.size());
		}

		evenDistribution = evenDistribution.div(divider);
		if (evenDistribution == 0) evenDistribution = 16;
		evenDistribution = evenDistribution.mul(bkm.expander);
		Logger::log(LogLevel::Info, "Shifting Random Mask by Distribution: " + evenDistribution.toString() + " Divider: " + util::formatThousands(divider));
		//Logger::log(LogLevel::Debug, "BetaKey Basis:" + _startingKeys.BetaKeys[0].toString());
		Logger::log(LogLevel::Debug, "KeySpace:" + keySpace.toString());
		Logger::log(LogLevel::Debug, "KeyA:" + bkm.keyA.toString());
		Logger::log(LogLevel::Debug, "KeyB:" + bkm.keyB.toString());
		for (uint64_t k = 0; k < _startingKeys.Keys.size();k++) {
			_startingKeys.Keys[k] = _startingKeys.Keys[k] + evenDistribution;
			if (shiftedKeys % REPORTING_SIZE == 0)  Logger::log(LogLevel::Info, "SAMPLE Key: " + _startingKeys.RootKeys[k].toString() + " -> " + _startingKeys.Keys[k].toString());
			shiftedKeys++;
		}
	}
	else if (distance_based) {
		keySpace = _endExponent.sub(_startExponent);

		while (divider <= 1) {
			divider++;
		}

		Logger::log(LogLevel::Debug, "Divider: " + util::formatThousands(divider));

		Logger::log(LogLevel::Info, "Shifting Keys by Distance-Variable Distribution,  Divider: " + util::formatThousands(divider));
		for (uint64_t k = 0; k < _startingKeys.Keys.size();k++) {
			_startingKeys.Keys[k] = _startingKeys.Keys[k] + (_startingKeys.Distances[k].div(divider));
			if (shiftedKeys % REPORTING_SIZE == 0)  Logger::log(LogLevel::Info, "SAMPLE Key: " + _startingKeys.RootKeys[k].toString() + " -> " + _startingKeys.Keys[k].toString());
			shiftedKeys++;
		}
	}
	else {
		keySpace = _endExponent.sub(_startExponent);
		
		Logger::log(LogLevel::Debug, "KeySpace: " + keySpace.toString());
		Logger::log(LogLevel::Debug, "Divider: " + util::formatThousands(divider));


		evenDistribution = keySpace.div(_startingKeys.Keys.size());
		evenDistribution = evenDistribution.div(divider);
		Logger::log(LogLevel::Info, "Shifting Keys by Distribution: " + evenDistribution.toString() + " Divider: " + util::formatThousands(divider));
		for (uint64_t k = 0; k < _startingKeys.Keys.size();k++) {
			_startingKeys.Keys[k] = _startingKeys.Keys[k] + evenDistribution;
			if (shiftedKeys % REPORTING_SIZE == 0)  Logger::log(LogLevel::Info, "SAMPLE Key: " + _startingKeys.RootKeys[k].toString() + " -> " + _startingKeys.Keys[k].toString());
			shiftedKeys++;
		}
	}





	Logger::log(LogLevel::Info, "Key Buffer Size: " + util::formatThousands(_startingKeys.Keys.size()));
	Logger::log(LogLevel::Info, "Writing " + util::formatThousands(_startingKeys.Keys.size()) + " keys to " + _info.name + "...");

	cudaCall(_deviceKeys->init(_blocks, _threads, _pointsPerThread, _startingKeys.Keys));


	// Show progress in 10% increments
	double pct = 10.0;
	for (int i = 1; i <= 256; i++) {
		cudaCall(_deviceKeys->doStep());

		if (((double)i / 256.0) * 100.0 >= pct) {
			if (pct <= 10.0)
				Logger::log2(LogLevel::Info, util::format("%.1f%%", pct));
			else
				printf("  %.1f%%", pct);
			pct += 10.0;
		}
	}
	printf("\n");

	Logger::log(LogLevel::Info, "Done");
}


void CudaKeySearchDevice::setTargets(const std::set<KeySearchTargetHash160>& targets)
{
	_targetsHash160.clear();

	for (std::set<KeySearchTargetHash160>::iterator i = targets.begin(); i != targets.end(); ++i) {
		hash160 h(i->value);
		_targetsHash160.push_back(h);
	}


	cudaCall(_targetLookup.setTargets(_targetsHash160));
}


void CudaKeySearchDevice::doStep()
{

	try {

		callKeyFinderKernel(_blocks, _threads, _pointsPerThread, false, _compression, _searchMode);

	}

	catch (cuda::CudaException ex) {
		Logger::log(LogLevel::Debug, "error hit: " + ex.msg );
		printf("Cuda error \"%s\".\n", cudaGetErrorString(ex.error));
		throw KeySearchException(ex.msg);
	}

	//Logger::log(LogLevel::Debug, "after CallKFK");
	getResultsInternal(_stride, _strideQueueIndex);

	_iterations++;
}

uint64_t CudaKeySearchDevice::keysPerStep()
{
	return (uint64_t)_blocks * _threads * _pointsPerThread;
}

std::string CudaKeySearchDevice::getDeviceName()
{
	return _deviceName;
}

void CudaKeySearchDevice::getMemoryInfo(uint64_t& freeMem, uint64_t& totalMem)
{
	cudaCall(cudaMemGetInfo(&freeMem, &totalMem));
}

void CudaKeySearchDevice::removeTargetFromListHash160(const unsigned int hash[5])
{
	size_t count = _targetsHash160.size();

	while (count) {
		if (memcmp(hash, _targetsHash160[count - 1].h, 20) == 0) {
			_targetsHash160.erase(_targetsHash160.begin() + count - 1);
			return;
		}
		count--;
	}
}


bool CudaKeySearchDevice::isTargetInListHash160(const unsigned int hash[5])
{

	size_t count = _targetsHash160.size();

	while (count) {
		if (memcmp(hash, _targetsHash160[count - 1].h, 20) == 0) {
			return true;
		}
		count--;
	}

	return false;
}


GPUMatrix CudaKeySearchDevice::getPrivateKeyOffset(int thread, int block, int idx)
{
	GPUMatrix result;

	result.thread = thread;
	result.block = block;
	result.idx = idx;

	// Total number of threads
	int totalThreads = _blocks * _threads;

	int base = idx * totalThreads;

	// Global ID of the current thread
	int threadId = block * _threads + thread;

	result.threadId = threadId;
	result.offset = base + threadId;

	return result;
}

void CudaKeySearchDevice::getResultsInternal(secp256k1::uint256 current_stride, int current_strideQueueIndex)
{
	//Logger::log(LogLevel::Debug, "getResultsInternal(" + current_stride.toString(16) + ", " + std::to_string(current_strideQueueIndex) + ");");
	int count = _resultList.size();
	int actualCount = 0;
	if (count == 0) {
		return;
	}

	unsigned char* ptr = new unsigned char[count * sizeof(CudaDeviceResult)];

	_resultList.read(ptr, count);

	for (int i = 0; i < count; i++) {
		struct CudaDeviceResult* rPtr = &((struct CudaDeviceResult*)ptr)[i];

		// might be false-positive
		if (!isTargetInListHash160(rPtr->digest)) {
			continue;
		}
		actualCount++;

		KeySearchResult minerResult;

		// Calculate the private key based on the number of iterations and the current thread
		secp256k1::uint256 offset;
		secp256k1::uint256 privateKey;

		//could rPtr->idx be wrong?
		GPUMatrix pkOffsetMatrix = getPrivateKeyOffset(rPtr->thread, rPtr->block, rPtr->idx);
		//uint32_t privateKeyOffset = getPrivateKeyOffset(rPtr->thread, rPtr->block, rPtr->idx);
		minerResult.offsetMatrix = pkOffsetMatrix;

		minerResult.stride = current_stride;
		minerResult.strideQueueIndex = current_strideQueueIndex;

		offset = secp256k1::uint256(_iterations) * current_stride;

		//privateKey = exponents[pkOffsetMatrix.offset];
		privateKey  = _startingKeys.Keys[pkOffsetMatrix.offset];
		minerResult.exponent = privateKey;

		privateKey = secp256k1::addModN(privateKey, offset);

		minerResult.privateKey = privateKey;
		minerResult.compressed = rPtr->compressed;

		memcpy(minerResult.hash, rPtr->digest, 20);

		minerResult.publicKey = secp256k1::ecpoint(secp256k1::uint256(rPtr->x, secp256k1::uint256::BigEndian), secp256k1::uint256(rPtr->y, secp256k1::uint256::BigEndian));

		/*
		if (_strideQueue.size()>0){
			minerResult.stride = _strideQueue[_strideQueueIndex];
		}
		*/

		removeTargetFromListHash160(rPtr->digest);

		_results.push_back(minerResult);
	}

	// Reload the bloom filters
	if (actualCount) {
		cudaCall(_targetLookup.setTargets(_targetsHash160));
	}


	delete[] ptr;

	_resultList.clear();

}

/*
// Verify a private key produces the public key and hash
bool CudaKeySearchDevice::verifyKeyHash160(const secp256k1::uint256& privateKey, const secp256k1::ecpoint& publicKey, const unsigned int hash[5], bool compressed)
{
	secp256k1::ecpoint g = secp256k1::G();

	secp256k1::ecpoint p = secp256k1::multiplyPoint(privateKey, g);

	if (!(p == publicKey)) {
		return false;
	}

	unsigned int xWords[8];
	unsigned int yWords[8];

	p.x.exportWords(xWords, 8, secp256k1::uint256::BigEndian);
	p.y.exportWords(yWords, 8, secp256k1::uint256::BigEndian);

	unsigned int digest[5];
	if (compressed) {
		Hash::hashPublicKeyCompressed(xWords, yWords, digest);
	}
	else {
		Hash::hashPublicKey(xWords, yWords, digest);
	}

	for (int i = 0; i < 5; i++) {
		if (digest[i] != hash[i]) {
			return false;
		}
	}

	return true;
}
*/

size_t CudaKeySearchDevice::getResults(std::vector<KeySearchResult>& resultsOut)
{
	for (int i = 0; i < _results.size(); i++) {
		resultsOut.push_back(_results[i]);
	}
	_results.clear();

	return resultsOut.size();
}

secp256k1::uint256 CudaKeySearchDevice::getNextKey()
{
	uint64_t totalPoints = (uint64_t)_pointsPerThread * _threads * _blocks;

	return _startExponent + secp256k1::uint256(totalPoints) * _iterations * _stride;
}

void CudaKeySearchDevice::updateStride(secp256k1::StrideMapEntry strideEntry, bool reset)
{
	_stride = strideEntry.stride;

	if (reset) reUseStartingPoints();

	cudaCall(allocateChainBuf(_threads * _blocks * _pointsPerThread));

	// Set the incrementor
	secp256k1::ecpoint g = secp256k1::G();
	secp256k1::ecpoint p = secp256k1::multiplyPoint(strideEntry.stride, g);		//1*_stride instead of matrix

	Logger::log(LogLevel::Debug, "Set Next Stride: " + strideEntry.stride.toString());
	cudaCall(_resultList.init(sizeof(CudaDeviceResult), 16));


	cudaCall(setIncrementorPoint(p.x, p.y));

	//resetTargets();
	_iterations = 0;
}

secp256k1::StrideMapEntry CudaKeySearchDevice::nextStride()
{
	secp256k1::uint256 zero = secp256k1::uint256(0);

	secp256k1::StrideMapEntry defaultResult;
	defaultResult.cycles = 0;
	defaultResult.directive = "";
	defaultResult.stride = 0;
	//defaultResult.currentStrideIndex = _strideQueueIndex;

	_strideQueueIndex += 1;
	if (_strideQueueIndex == _strideQueue.size()) return defaultResult;			//bail out at the end.

	secp256k1::StrideMapEntry sme = _strideQueue[_strideQueueIndex];
	//Logger::log(LogLevel::Debug, "Directive: " + sme.directive);

	bool resetPoints = (sme.directive == "reset");								//reset directive?   set the boolean and bump up to the next iterat ion.
	if (resetPoints) _strideQueueIndex++;

	bool regenPoints = (sme.directive == "regenerate");
	if (regenPoints)
	{
		//_strideQueueIndex++;
		reGenerateStartingPoints();
		//generateStartingPoints();
		return nextStride();
	}

	if (_strideQueueIndex == _strideQueue.size()) return defaultResult;			//bail out at the end.

	sme = _strideQueue[_strideQueueIndex];										//if the index changed - load the next one
	if (_strideQueueIndex < _strideQueue.size()) {

		_stride = sme.stride;

		updateStride(sme, resetPoints);

		//sme.currentStrideIndex = _strideQueueIndex;
		return sme;
	}
	else
	{
		return defaultResult;
	}
}

void CudaKeySearchDevice::resetCuda()
{
	_iterations = 0;
	
	_deviceKeys->clearPrivateKeys();
	_deviceKeys->clearPublicKeys();
	_deviceKeys->clearProgress();

	resetDevice();
}

