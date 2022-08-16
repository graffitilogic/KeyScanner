#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "KeyFinder.h"
#include "../Util/util.h"
#include "../AddrUtil/AddressUtil.h"
#include "../RandomHelper.h"
#include "../Logger/Logger.h"
#include <chrono>


void KeyFinder::defaultResultCallback(KeySearchResult result)
{
	// Do nothing
}

void KeyFinder::defaultStatusCallback(KeySearchStatus status)
{
	// Do nothing
}

KeyFinder::KeyFinder(CudaKeySearchDevice*& cudaDvc, const secp256k1::uint256& startKey, const secp256k1::uint256& endKey, int compression, int searchMode,
	const secp256k1::StrideMapEntry& stride, const std::string& key_mask, const secp256k1::uint256& skip, int netSize, int randFill, 
	uint64_t max_cycles, uint64_t auto_strides, uint64_t max_seconds, uint64_t max_hours, bool randomStride, bool continueAfterEnd,
	uint32_t randomSrtrideBits)
{
	_device = cudaDvc;

	_totalTime = 0;
	_running = false;
	_total = 0;
	_statusInterval = 1000;

	//_device = device;

	//std::vector<DeviceManager::DeviceInfo> _devices = DeviceManager::getDevices();
	//_device = new CudaKeySearchDevice(deviceId, threads, pointsperThread, blocks);

	_compression = compression;
	_searchMode = searchMode;

	_startKey = startKey;

	_endKey = endKey;

	_statusCallback = NULL;

	_resultCallback = NULL;

	_iterCount = 0;

	//_totalIterationCount = 0;
	//_totalPreviousIterationCount = 0;

	_stride = stride.stride;
	if (_stride == 0) _stride = 1;

	_keyMask = key_mask;
	_netSize = netSize;

	_randFill = randFill;

	_skip = skip;

	_autoStrides = auto_strides;

	//_currentStrideIndex = 0;
	//_maxStrideIndex = 0;

	//_strideQueueSize = 0;
	//_strideQueueIndex = 0;
	
	_maxCycles = (stride.cycles > 0) ? stride.cycles : max_cycles;
	_maxSeconds = max_seconds;
	_maxHours = max_hours;

	_lastHour = 0;
	_totalHours = 0;

	_randomStride = randomStride;
	_continueAfterEnd = continueAfterEnd;
	_randomSrtrideBits = randomSrtrideBits;
	_rStrideCount = 1;
	_rStrideHistory.clear();
	

	if (_randomStride) _stride = secp256k1::getRandom128(_randomSrtrideBits, _rStrideHistory);
	Logger::log(LogLevel::Info, "Counting by : [" + _stride.toString() + "] (" + std::to_string(_stride.getBitRange()) + " bit)");
}

KeyFinder::KeyFinder()
{
	_autoStrides = 0;
	_compression = 0;
	_continueAfterEnd = false;
	//_currentStrideIndex = 0;
	_device = NULL;
	_iterCount = 0;
	_lastHour = 0;
	_maxCycles = 0;
	_maxHours = 0;
	_maxSeconds = 0;
	//_maxStrideIndex = 0;
	_rStrideCount = 0;
	_randomSrtrideBits = 0;
	_randomStride = false;
	_resultCallback = NULL;
	_running = false;
	_searchMode = 0;
	_statusCallback = NULL;
	_statusInterval = 0;
	_total = 0;
	_totalHours = 0;
	_totalIterationCount = 0;
	_totalTime = 0;
}

KeyFinder::~KeyFinder()
{
	//Logger::log(LogLevel::Debug, "delete KeyFinder()");
	//delete _device;
	//_device = NULL;
}

void KeyFinder::setTargets(std::vector<std::string>& targets)
{
	if (targets.size() == 0) {
		throw KeySearchException("Requires at least 1 target");
	}

	_targetsHash160.clear();

	if (_searchMode == SearchMode::ADDRESS) {

		// Convert each address from base58 encoded form to a 160-bit integer
		for (unsigned int i = 0; i < targets.size(); i++) {

			if (!Address::verifyAddress(targets[i])) {
				throw KeySearchException("Invalid address '" + targets[i] + "'");
			}

			KeySearchTargetHash160 t;

			Base58::toHash160(targets[i], t.value);

			_targetsHash160.insert(t);
		}

		_device->setTargets(_targetsHash160);

	}
	else {
		throw KeySearchException("Invalid SearchMode");
	}
}

void KeyFinder::setTargets(std::string targetsFile)
{
	std::ifstream inFile(targetsFile.c_str());

	if (!inFile.is_open()) {
		Logger::log(LogLevel::Error, "Unable to open '" + targetsFile + "'");
		throw KeySearchException();
	}

	_targetsHash160.clear();

	std::string line;
		Logger::log(LogLevel::Info, "Loading addresses from '" + targetsFile + "'");
		while (std::getline(inFile, line)) {
			util::removeNewline(line);
			line = util::trim(line);

			if (line.length() > 0) {
				if (!Address::verifyAddress(line)) {
					Logger::log(LogLevel::Error, "Invalid address '" + line + "'");
					throw KeySearchException();
				}

				KeySearchTargetHash160 t;

				Base58::toHash160(line, t.value);

				_targetsHash160.insert(t);
			}
		}
		Logger::log(LogLevel::Info, util::formatThousands(_targetsHash160.size()) + " addresses loaded ("
			+ util::format("%.1f", (double)(sizeof(KeySearchTargetHash160) * _targetsHash160.size()) / (double)(1024 * 1024)) + "MB)");

		_device->setTargets(_targetsHash160);
}

void KeyFinder::setResultCallback(void(*callback)(KeySearchResult))
{
	_resultCallback = callback;
}

void KeyFinder::setStatusCallback(void(*callback)(KeySearchStatus))
{
	_statusCallback = callback;
}

void KeyFinder::setStatusInterval(uint64_t interval)
{
	_statusInterval = interval;
}



void KeyFinder::init(int deviceId, int threads, int pointsperThread, int blocks, int step, secp256k1::StrideMapEntry sme, secp256k1::KeyBuffers startingKeys)
{

	//derive a max stride from the endRange value
	//secp256k1::uint256 maxStride;

	_strideMapEntry = sme;

	//_currentStrideIndex = sme.strideQueueIndex;
	//_maxStrideIndex = sme.strideQueueMax;

	Logger::log(LogLevel::Info, "Initializing " + _device->getDeviceName());

	//Logger::log(LogLevel::Info, "(Device) Initializing " + _device->getDeviceName() + " Step: " + std::to_string(sme.strideQueueIndex) + " , Stride: " + sme.stride.toString() + ", Stride Directive: " + sme.directive);
	_device->init(_startKey, _endKey, _compression, _searchMode, step, sme, _keyMask, startingKeys, _skip);
}

void KeyFinder::stop()
{
	_running = false;
}

secp256k1::KeyBuffers KeyFinder::getKeyBuffers() {
	return _device->getKeyBuffers();
}


void KeyFinder::removeTargetFromListHash160(const unsigned int hash[5])
{
	KeySearchTargetHash160 t(hash);

	_targetsHash160.erase(t);
}


bool KeyFinder::isTargetInListHash160(const unsigned int hash[5])
{
	KeySearchTargetHash160 t(hash);
	return _targetsHash160.find(t) != _targetsHash160.end();
}


void KeyFinder::run()
{

	uint64_t pointsPerIteration = _device->keysPerStep();

	bool MAX_TIME_REACHED = false;
	bool MAX_CYCLES_REACHED = false;

	_running = true;

	util::Timer timer;

	timer.start();

	uint64_t step = 0;
	uint64_t prevIterCount = 0;
	uint64_t prevTotalIterationCount = 0;

	_totalTime = 0;

	if (_strideMapEntry.directive != "stride") _running = false;
	while (_running) {

		_device->doStep();
		step++;

		_iterCount++;
		_totalIterationCount++;

		// Update status
		uint64_t t = timer.getTime();

		if (t >= _statusInterval) {

			KeySearchStatus info;
			uint64_t count;

			if (_restartCount == 0)
				count = (_iterCount - prevIterCount) * pointsPerIteration;
			else {
				(_totalIterationCount - prevTotalIterationCount)* pointsPerIteration;
			}

			_total += count;

			double seconds = (double)t / 1000.0;

			info.speed = (double)((double)count / seconds) / 1000000.0;

			info.total = _total;

			info.totalTime = _totalTime;

			unsigned int hours = ((unsigned int)(_totalTime / 1000) % 86400) / 3600;
			unsigned int running_seconds = ((unsigned int)(_totalTime / 1000));

			//std::cout << hours;
			//std::cout << seconds << "||" << _maxSeconds;

			if (_lastHour != hours) {
				//hours is the hours portion of full time so, 23 then rolls to 0.   
				_totalHours++;
			}

			if ((_maxHours > 0) && _totalHours >= _maxHours) MAX_TIME_REACHED = true;
			if ((_maxSeconds > 0) && running_seconds >= _maxSeconds) MAX_TIME_REACHED = true;
			if ((_maxCycles > 0) &&  step >= _maxCycles) MAX_CYCLES_REACHED = true;
			//if ((_maxCycles > 0) && _iterCount >= _maxCycles) MAX_CYCLES_REACHED = true;

			//if (step >= 64) MAX_CYCLES_REACHED = true;

			_lastHour = hours;

			uint64_t freeMem = 0;

			uint64_t totalMem = 0;

			_device->getMemoryInfo(freeMem, totalMem);

			info.freeMemory = freeMem;
			info.deviceMemory = totalMem;
			info.deviceName = _device->getDeviceName();
			info.targets =  _targetsHash160.size();
			//info.totalDistance = _totalIterationCount;
			info.distance = _iterCount;
			info.nextKey = getNextKey();
			info.stride = _stride;
			info.rStrideCount = _rStrideCount;
			info.currentStrideIndex = _strideMapEntry.strideQueueIndex;
			info.maxStrideIndex = _strideMapEntry.strideQueueMax;

			_statusCallback(info);

			timer.start();

			prevIterCount = _iterCount;
			prevTotalIterationCount = _totalIterationCount;

			_totalTime += t;
		}

		std::vector<KeySearchResult> results;

		if (_device->getResults(results) > 0) {

			for (unsigned int i = 0; i < results.size(); i++) {

				KeySearchResult info;
				info.privateKey = results[i].privateKey;
				info.publicKey = results[i].publicKey;
				info.compressed = results[i].compressed;
				info.address = Address::fromPublicKey(results[i].publicKey, results[i].compressed);
				//info.strideQueueIndex = _strideQueueIndex;
				//info.stride = _stride;
				info.exponent = results[i].exponent;
				info.offsetMatrix = results[i].offsetMatrix;
				info.stride = results[i].stride;
				info.strideQueueIndex = results[i].strideQueueIndex;
			
				_resultCallback(info);
			}

			// Remove the hashes that were found
				for (unsigned int i = 0; i < results.size(); i++) {
					removeTargetFromListHash160(results[i].hash);
				}

		}

		// Stop if there are no keys left
		if (_targetsHash160.size() == 0)  {
			printf("\n");
			Logger::log(LogLevel::Info, "No targets remaining");
			_running = false;
		}

		if (MAX_TIME_REACHED) {
			printf("\n");
			Logger::log(LogLevel::Info, "Max Time Reached");
			_running = false;
		}


		if (MAX_CYCLES_REACHED) {
			printf("\n");
			Logger::log(LogLevel::Info, "Max Cycles Reached");
			//stop();
			_running = false;
		}

		// Stop if we searched the entire range
		if (_device->getNextKey().cmp(_endKey) >= 0 || _device->getNextKey().cmp(_startKey) < 0) {
			//printf("\n");

			if (_continueAfterEnd && _randomStride) {
				//Logger::log(LogLevel::Info, "Continuing after end...");
				reSetupEverything();
			}
			else {
				//temporarily disabled until I can do the stop-after-seconds
				//printf("\n");
				//Logger::log(LogLevel::Info, "Reached end of keyspace");
				//_running = false;
			}
		}
	}
}

secp256k1::uint256 KeyFinder::getNextKey()
{
	return _device->getNextKey();
}

uint64_t KeyFinder::LZC(uint64_t x)
{
	uint64_t c = 0;
	if (x == 0)
		return 64ULL;
	for (int i = 63; i >= 0; i--) {
		if ((1ULL & (uint64_t)x >> (uint64_t)i) == 0)
			c++;
		else
			return c;
	}
	return c;
}

uint64_t KeyFinder::TZC(uint64_t x)
{
	uint64_t c = 0;
	if (x == 0)
		return 64ULL;
	for (int i = 0; i <= 63; i++) {
		if ((1ULL & (uint64_t)x >> (uint64_t)i) == 0)
			c++;
		else
			return c;
	}
	return c;
}


void KeyFinder::reSetupEverything()
{
	secp256k1::StrideMapEntry sme;
	_stride = secp256k1::getRandom128(_randomSrtrideBits, _rStrideHistory);
	sme.stride = _stride;
	sme.directive = "stride";

	//Logger::log(LogLevel::Info, "Counting by : " + _stride.toString() + " (" + std::to_string(_stride.getBitRange()) + " bit)");
	_device->updateStride(sme, false);
	_rStrideCount++;
}
