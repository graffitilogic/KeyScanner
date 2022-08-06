#include <stdio.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>

#include "KeyFinder/KeyFinder.h"
#include "AddrUtil/AddressUtil.h"
#include "Util/util.h"
#include "Secp256k1/secp256k1.h"
#include "CmdParse/CmdParse.h"
#include "Logger/Logger.h"
#include "KeyFinder/ConfigFile.h"
#include "KeyFinder/DeviceManager.h"

#include "CudaDevice/CudaKeySearchDevice.h"

#include <gmp.h>
#include <gmpxx.h>
#include <sstream>
#include "RandomHelper.h"
#include <chrono>

#define RELEASE "1.02"



typedef struct _RunConfig {
	// startKey is the first key. We store it so that if the --continue
	// option is used, the correct progress is displayed. startKey and
	// nextKey are only equal at the very beginning. nextKey gets saved
	// in the checkpoint file.
	secp256k1::uint256 startKey = 1;
	secp256k1::uint256 nextKey = 1;

	// The last key to be checked
	secp256k1::uint256 endKey = secp256k1::N - 1;
	secp256k1::uint256 range;

	uint64_t statusInterval = 1800;
	uint64_t checkpointInterval = 60000;

	unsigned int threads = 0;
	unsigned int blocks = 0;
	unsigned int pointsPerThread = 0;

	int compression = PointCompressionType::COMPRESSED;
	int searchMode = SearchMode::ADDRESS;


	std::vector<std::string> targets;

	std::string targetsFile = "";

	std::string checkpointFile = "";

	std::string strideMapFile = "";

	int device = 0;

	std::string resultsFile = "Found.txt";

	uint64_t totalkeys = 0;
	unsigned int elapsed = 0;

	secp256k1::uint256 stride = 1;
	secp256k1::uint256 skip = 0;
	secp256k1::uint256 keyspaceoverride = 0;

	std::string keyMask = "";

	uint64_t maxHours = 0;
	uint64_t maxSeconds = 0;
	uint64_t maxCycles = 0;
	uint64_t autoStrides = 0;

	int netSize = 0;
	int randFill = 0;

	bool randomStride = false;
	bool continueAfterEnd = false;
	uint32_t randomSrtrideBits = 2;

	bool follow = false;
}RunConfig;

static RunConfig _config;

std::vector<DeviceManager::DeviceInfo> _devices;
secp256k1::KeyBuffers _startKeys;


static uint64_t _lastUpdate = 0;
static uint64_t _runningTime = 0;
static uint64_t _startTime = 0;


// ((input - min) * 100) / (max - min)
double getPercantage(const secp256k1::uint256& val)
{
	mpz_class x(((secp256k1::uint256)val).toString().c_str(), 16);
	mpz_class r(_config.range.toString().c_str(), 16);
	x = x - mpz_class(_config.startKey.toString().c_str(), 16);
	x = x * 100;
	mpf_class y(x);
	y = y / mpf_class(r);
	return y.get_d();
}

/**
* Callback to display the private key
*/
void resultCallback(KeySearchResult info)
{
	printf("\n");
	if (_config.resultsFile.length() != 0) {
		Logger::log(LogLevel::Info, "Found key for address '" + info.address + "'. Written to '" + _config.resultsFile + "'");
		uint64_t timeStamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		std::stringstream ss;
		ss << "*************************************************************************\n";
		ss << "privateKey: " + info.privateKey.toString(16) + "\n";
		ss << "address: " + info.address + "\n";
		ss << "exponent: " + info.exponent.toString(16) + "\n";
		ss << "public key (compressed): " + info.publicKey.toString(info.compressed) + "\n";
		ss << "public key (x): " + info.publicKey.x.toString(16) + "\n";
		ss << "public key (y): " + info.publicKey.y.toString(16) + "\n";
		ss << "stride: " + info.stride.toString(16) + "\n";
		ss << "stride queue index: " + std::to_string(info.strideQueueIndex) + "\n";
		ss << "block: " + std::to_string(info.offsetMatrix.block) + "\n";
		ss << "idx: " + std::to_string(info.offsetMatrix.idx) + "\n";
		ss << "thread: " + std::to_string(info.offsetMatrix.thread) + "\n";
		ss << "threadid: " + std::to_string(info.offsetMatrix.threadId) + "\n";
		ss << "offset: " + std::to_string(info.offsetMatrix.offset) + "\n";
		ss << "timestamp: " + std::to_string(timeStamp) + "\n";
		ss << "*************************************************************************\n\n";
		std::string s = ss.str();
		util::appendToFile(_config.resultsFile, s);

		//return;
	}

	std::string logStr = "Address     : " + info.address + "\n";
	logStr += "Private key : " + info.privateKey.toString(16) + "\n";
	logStr += "Compressed  : ";

	if (info.compressed) {
		logStr += "yes\n";
	}
	else {
		logStr += "no\n";
	}

	logStr += "Public key  : ";

	if (info.compressed) {
		logStr += info.publicKey.toString(true) + "\n";
	}
	else {
		logStr += info.publicKey.x.toString(16) + "\n";
		logStr += info.publicKey.y.toString(16) + "\n";
	}

	//logStr += "Stride  : " + info.stride.toString();

	Logger::log(LogLevel::Info, logStr);
}

/**
Callback to display progress
*/
void statusCallback(KeySearchStatus info)
{
	std::string speedStr;// = std::string("SPEED: ");

	if (info.speed < 0.01) {
		speedStr = "< 0.01 MK/s";
	}
	else {
		speedStr = util::format("%.2f", info.speed) + " MK/s";
	}

	std::string totalStr = util::formatThousands(_config.totalkeys + info.total);
	std::string timeStr = util::formatSeconds((unsigned int)((_config.elapsed + info.totalTime) / 1000));

	uint64_t usedMemoryGb = ((info.deviceMemory - info.freeMemory) / (1024 * 1024)) / 1024;
	std::string usedMemStr = util::format(usedMemoryGb);

	uint64_t totalMemoryGb = (info.deviceMemory / (1024 * 1024)) / 1024;
	std::string totalMemStr = util::format(totalMemoryGb);

	std::string targetStr = util::format(info.targets);
	std::string cyclesStr = util::formatThousands(info.distance);
	//std::string globalCyclesStr = util::formatThousands(info.totalDistance);

	std::string autoStridesStr = util::formatThousands(info.currentStrideIndex) + "/" + util::formatThousands(info.maxStrideIndex);
	//std::string stridesStr = std::to_string(info.stride.getBitRange()) + "bits";
	// Fit device name in 16 characters, pad with spaces if less
	//	std::string devName = info.deviceName.substr(0, 16);
	//	devName += std::string(16 - devName.length(), ' ');
	
	
	//So, this might fuck up for cards with wierd names but it beats the shit out of "NVIDIA GeForce R", which is pointless output IMO - the other changes here are cuda-only anyway.
	std::vector<std::string> deviceName = util::split(info.deviceName, ' ');
	std::string devName = deviceName[0] + " " + deviceName[2] + deviceName[3];

	std::string clrLine = "\r";

	const char* formatStr = NULL;

	if (_config.follow) {
		//formatStr = "[DEV: %s %s/%sMB] [K: %s (%d bit), C: %lf %%] [I: %s (%d bit), %lu] [T: %s] [S: %s] [%s (%d bit)] [%s]\n";
		formatStr = "[%s %s/%sGB] [S: %s] [%s (%d bit)] [%s]\n";
	}
	else {
		//formatStr = "\r[DEV: %s %s/%sMB] [K: %s (%d bit), C: %lf %%] [I: %s (%d bit), %lu] [T: %s] [S: %s] [%s (%d bit)] [%s] ";
		if (_config.autoStrides > 0 || _config.strideMapFile.size() >0) {
			formatStr = "\r[%s %s/%sGB] [S: %s] [%s (%d bit)] [%s strides]  [%s local cycles] [%s] ";
			//formatStr = "\r[%s %s/%sGB] [S: %s] [%s (%d bit)] [%s strides]  [%s local cycles] [%s total cycles] [%s] ";
		}
		else {
			formatStr = "\r[%s %s/%sGB] [S: %s] [%s (%d bit)] [%s cycles] [%s] ";
		}
	}

	//clear the line.
	for (int i = 0; i < strlen(formatStr) * 2;i++) {
		clrLine += " ";
	}


	if (_config.autoStrides > 0 || _config.strideMapFile.size() > 0) {
		printf("%s", clrLine.c_str());
		printf(formatStr, devName.c_str(), usedMemStr.c_str(), totalMemStr.c_str(), speedStr.c_str(), totalStr.c_str(), secp256k1::uint256(_config.totalkeys + info.total).getBitRange(), autoStridesStr, cyclesStr, timeStr.c_str());
		//printf(formatStr, devName.c_str(), usedMemStr.c_str(), totalMemStr.c_str(), speedStr.c_str(), totalStr.c_str(), secp256k1::uint256(_config.totalkeys + info.total).getBitRange(), autoStridesStr, cyclesStr, globalCyclesStr, timeStr.c_str());
	}
	else {
		printf(formatStr, devName.c_str(), usedMemStr.c_str(), totalMemStr.c_str(), speedStr.c_str(), totalStr.c_str(), secp256k1::uint256(_config.totalkeys + info.total).getBitRange(), cyclesStr,  timeStr.c_str());
	}

}

/**
 * Parses the start:end key pair. Possible values are:
 start
 start:end
 start:+offset
 :end
 :+offset
 */
bool parseKeyspace(const std::string& s, secp256k1::uint256& start, secp256k1::uint256& end)
{
	size_t pos = s.find(':');

	if (pos == std::string::npos) {
		start = secp256k1::uint256(s);
		end = secp256k1::N - 1;
	}
	else {
		std::string left = s.substr(0, pos);

		if (left.length() == 0) {
			start = secp256k1::uint256(1);
		}
		else {
			start = secp256k1::uint256(left);
		}

		std::string right = s.substr(pos + 1);

		if (right[0] == '+') {
			end = start + secp256k1::uint256(right.substr(1));
		}
		else {
			end = secp256k1::uint256(right);
		}
	}

	return true;
}

void usage()
{
	printf("OPTIONS [TARGETS]\n");
	printf("Where TARGETS is one or more addresses\n\n");

	printf("--help                       Display this message\n");
	printf("-c, --compressed             Use compressed points\n");
	printf("-u, --uncompressed           Use Uncompressed points\n");
	printf("--compression  MODE          Specify compression where MODE is\n");
	printf("                                 COMPRESSED or UNCOMPRESSED or BOTH\n");
	printf("-d, --device ID              Use device ID\n");
	printf("-t, --threads N              N threads per block\n");
	printf("-p, --points N               N points per thread\n");
	printf("-i, --in FILE                Read addresses from FILE, one per line\n");
	printf("-o, --out FILE               Write keys to FILE\n");
	printf("-f, --follow                 Follow text output\n");
	printf("--list-devices               List available devices\n");
	printf("--keyspace KEYSPACE          Specify the keyspace:\n");
	printf("                                 START:END\n");
	printf("                                 START:+COUNT\n");
	printf("                                 START\n");
	printf("                                 :END\n");
	printf("                                 :+COUNT\n");
	printf("                             Where START, END, COUNT are in hex format\n");
	printf("--stride N                   Increment by N keys at a time\n");
	printf("--keyMask  N				 Key Mask for Starting Points\n");
	printf("--skip N			         Forward offset for starting points for start of scan.   Sort of like a manual continue to be used with --keysrc\n");
	printf("--netsize N                  Net Size (enables KeyFishing)\n");
	printf("--randomfill N               Random Point Fill % (when KeyFishing)\n");
	printf("--maxhrs N					 Stop after n Hours\n");
	printf("--maxseconds N			     Stop after n Seconds\n");
	printf("--maxcycles N			     Stop after n Cycles\n");
	printf("--autostrides N			     Automatically Shift the strides [b] times\n");
	printf("--stridemap FILE             Read Strides from FILE, see /tests/stridemap.txt for example\n");
	printf("-v, --version                Show version\n");
}


/**
 Finds default parameters depending on the device
 */
typedef struct {
	int threads;
	int blocks;
	int pointsPerThread;
}DeviceParameters;

DeviceParameters getDefaultParameters(const DeviceManager::DeviceInfo& device)
{
	DeviceParameters p;
	p.threads = 256;
	p.blocks = 32;
	p.pointsPerThread = 32;

	return p;
}

/*

static KeySearchDevice* getDeviceContext(DeviceManager::DeviceInfo& device, int blocks, int threads, int pointsPerThread)
{
	if (device.type == DeviceManager::DeviceType::CUDA) {
		return new CudaKeySearchDevice((int)device.physicalId, threads, pointsPerThread, blocks);
	}
	return NULL;
}

*/

static void printDeviceList(const std::vector<DeviceManager::DeviceInfo>& devices)
{
	for (int i = 0; i < devices.size(); i++) {
		printf("ID:     %d\n", devices[i].id);
		printf("Name:   %s\n", devices[i].name.c_str());
		printf("Memory: %lldMB\n", devices[i].memory / ((uint64_t)1024 * 1024));
		printf("Compute units: %d\n", devices[i].computeUnits);
		printf("\n");
	}
}

bool readAddressesFromFile(const std::string& fileName, std::vector<std::string>& lines)
{
	if (fileName == "-") {
		return util::readLinesFromStream(std::cin, lines);
	}
	else {
		return util::readLinesFromStream(fileName, lines);
	}
}

int parseCompressionString(const std::string& s)
{
	std::string comp = util::toLower(s);

	if (comp == "both") {
		return PointCompressionType::BOTH;
	}

	if (comp == "compressed") {
		return PointCompressionType::COMPRESSED;
	}

	if (comp == "uncompressed") {
		return PointCompressionType::UNCOMPRESSED;
	}

	throw std::string("Invalid compression format: '" + s + "'");
}

int parseSearchModeString(const std::string& s)
{
	std::string stype = util::toLower(s);

	if (stype == "address") {
		return SearchMode::ADDRESS;
	}

	if (stype == "xpoint") {
		return  SearchMode::XPOINT;
	}

	throw std::string("Invalid search mode format: '" + s + "'");
}

static std::string getCompressionString(int mode)
{
	switch (mode) {
	case PointCompressionType::BOTH:
		return "both";
	case PointCompressionType::UNCOMPRESSED:
		return "uncompressed";
	case PointCompressionType::COMPRESSED:
		return "compressed";
	}

	throw std::string("Invalid compression setting '" + util::format(mode) + "'");
}

static std::string getSearchModeString(int mode)
{
	switch (mode) {
	case SearchMode::ADDRESS:
		return "ADDRESS";
	case SearchMode::XPOINT:
		return "XPOINT";
	}

	throw std::string("Invalid search mode setting '" + util::format(mode) + "'");
}

void doSearchStep(CudaKeySearchDevice*& cudaDvc, secp256k1::StrideMapEntry lastStride, secp256k1::StrideMapEntry sme, int step) {
	// Get device context
	//KeySearchDevice* device = getDeviceContext(_devices[_config.device], _config.blocks, _config.threads, _config.pointsPerThread);
	//CudaKeySearchDevice* device = new CudaKeySearchDevice((int)_devices[_config.device].physicalId, _config.threads, _config.pointsPerThread, _config.blocks);

	KeyFinder* finder = new KeyFinder(cudaDvc, _config.nextKey, _config.endKey, _config.compression, _config.searchMode,
		sme, _config.keyMask, _config.skip, _config.netSize, _config.randFill,
		_config.maxCycles, _config.autoStrides, _config.maxSeconds, _config.maxHours, _config.randomStride, _config.continueAfterEnd,
		_config.randomSrtrideBits);

	finder->setResultCallback(resultCallback);
	finder->setStatusInterval(_config.statusInterval);
	finder->setStatusCallback(statusCallback);


	//if (lastStride.directive != "stride") sme.directive = lastStride.directive; //essentially rolls up the directive only strides with the stride=strides.
	finder->init((int)_devices[_config.device].physicalId, _config.threads, _config.pointsPerThread, _config.blocks, step, sme, _startKeys);
	if (!_config.targetsFile.empty()) {
		finder->setTargets(_config.targetsFile);
	}
	else {
		finder->setTargets(_config.targets);
	}

	finder->run();

	_startKeys = finder->getKeyBuffers();
	//if (_startKeys.Keys.size() == 0 || sme.directive=="regenerate") _startKeys = finder->getKeyBuffers();  //reusable start keys after first iteration

	cudaDvc->resetCuda();

	//finder->reset();	
	delete finder;
	finder = NULL;

}

void showStrideQueue(std::vector<secp256k1::StrideMapEntry> strideQueue) {
	std::stringstream ss;
	for (int s = 0; s < strideQueue.size(); s++) {
		secp256k1::StrideMapEntry sme = strideQueue[s];
		ss << sme.strideQueueIndex << "|" << sme.cycles << "|" << sme.directive << "|" << sme.stride.toString() << "|" << sme.strideStr << "|" << sme.strideQueueMax << "\n";
	}
	std::string output = ss.str();
	printf(output.c_str());
}

int run()
{
	if (_config.device < 0 || _config.device >= _devices.size()) {
		Logger::log(LogLevel::Error, "device " + util::format(_config.device) + " does not exist");
		return 1;
	}

	_config.range = _config.endKey;
	_config.range = _config.range.sub(_config.nextKey);

	Logger::log(LogLevel::Info, "Compression : " + getCompressionString(_config.compression));
	Logger::log(LogLevel::Info, "Search mode : " + getSearchModeString(_config.searchMode));
	Logger::log(LogLevel::Info, "Starting at : " + _config.nextKey.toString() + " (" + std::to_string(_config.nextKey.getBitRange()) + " bit)");
	Logger::log(LogLevel::Info, "Ending at   : " + _config.endKey.toString() + " (" + std::to_string(_config.endKey.getBitRange()) + " bit)");
	Logger::log(LogLevel::Info, "Range       : " + _config.range.toString() + " (" + std::to_string(_config.range.getBitRange()) + " bit)");
	Logger::log(LogLevel::Info, "Stride      : " + _config.stride.toString());


	if (_config.randomStride) Logger::log(LogLevel::Info, "RStride     : " + util::format(_config.randomSrtrideBits) + " bit");

	if (_config.keyMask !="") Logger::log(LogLevel::Info, "Key Mask  : " + _config.keyMask);

	if (_config.randFill > 0) {
		Logger::log(LogLevel::Info, "Random Fill:  " + std::to_string(_config.randFill) + "%");
	}

	if (_config.netSize > 0) {
		Logger::log(LogLevel::Info, "Net Size:  " +  std::to_string(_config.netSize));
	}

	if (_config.maxHours > 0) {
		//prep output for hours
		std::stringstream ss_hrs;
		ss_hrs << _config.maxHours;
		std::string strMaxHrs = ss_hrs.str();

		Logger::log(LogLevel::Info, "Stopping after: " + strMaxHrs + " hrs");
	}

	if (_config.maxSeconds > 0) {
		//prep output for seconds
		std::stringstream ss_Seconds;
		ss_Seconds << _config.maxSeconds;
		std::string strMaxSeconds = ss_Seconds.str();

		Logger::log(LogLevel::Info, "Stopping after: " + strMaxSeconds + " seconds");
	}


	if (_config.maxCycles > 0) {
		//prep output forcycles
		std::stringstream ss_Cycles;
		ss_Cycles << _config.maxCycles;
		std::string strMaxCycles = ss_Cycles.str();

		Logger::log(LogLevel::Info, "Stopping after: " + strMaxCycles + " cycles");
	}
	//Logger::log(LogLevel::Info, "Counting by : " + _config.stride.toString() + " (" + std::to_string(_config.stride.getBitRange()) + " bit)");

	try {

		_lastUpdate = util::getSystemTime();
		_startTime = util::getSystemTime();

		// Use default parameters if they have not been set
		DeviceParameters params = getDefaultParameters(_devices[_config.device]);

		if (_config.blocks == 0) {
			_config.blocks = params.blocks;
		}

		if (_config.threads == 0) {
			_config.threads = params.threads;
		}

		if (_config.pointsPerThread == 0) {
			_config.pointsPerThread = params.pointsPerThread;
		}

		std::vector<secp256k1::StrideMapEntry> strideQueue;
		if (!_config.strideMapFile.empty()) {
			strideQueue = util::parseStrideFile(_config.strideMapFile);
		}

		//std::string lastDirective = "";
		//showStrideQueue(strideQueue);return 0;

		secp256k1::StrideMapEntry lastStride;
		lastStride = lastStride.emptyStride();
					
		// Get device context
		CudaKeySearchDevice* dvc = new CudaKeySearchDevice((int)_devices[_config.device].physicalId, _config.threads, _config.pointsPerThread, _config.blocks);

		for (int s = 0; s < strideQueue.size(); s++) {
			secp256k1::StrideMapEntry sme = strideQueue[s];
			//Logger::log(LogLevel::Debug, "Stride: " + sme.directive);

			if (true) {// (strideQueue[s].directive == "stride") {
				doSearchStep(dvc, lastStride, sme, s);
			}
			lastStride = strideQueue[s];
		}

	}
	catch (KeySearchException ex) {
		Logger::log(LogLevel::Info, "Error: " + ex.msg );
		return 1;
	}

	return 0;
}




/**
 * Parses a string in the form of x/y
 */
bool parseShare(const std::string& s, uint32_t& idx, uint32_t& total)
{
	size_t pos = s.find('/');
	if (pos == std::string::npos) {
		return false;
	}

	try {
		idx = util::parseUInt32(s.substr(0, pos));
	}
	catch (...) {
		return false;
	}

	try {
		total = util::parseUInt32(s.substr(pos + 1));
	}
	catch (...) {
		return false;
	}

	if (idx == 0 || total == 0) {
		return false;
	}

	if (idx > total) {
		return false;
	}

	return true;
}

int main(int argc, char** argv)
{
	//return  RandomHelper::randTest(); // CudaRand.randTest();

	bool optCompressed = false;
	bool optUncompressed = false;
	bool listDevices = false;
	bool optShares = false;
	bool optThreads = false;
	bool optBlocks = false;
	bool optPoints = false;

	uint32_t shareIdx = 0;
	uint32_t numShares = 0;

	// Catch --help first
	for (int i = 1; i < argc; i++) {
		if (std::string(argv[i]) == "--help") {
			usage();
			return 0;
		}
	}

	// Check for supported devices
	try {
		_devices = DeviceManager::getDevices();

		if (_devices.size() == 0) {
			Logger::log(LogLevel::Error, "No devices available");
			return 1;
		}
	}
	catch (DeviceManager::DeviceManagerException ex) {
		Logger::log(LogLevel::Error, "Error detecting devices: " + ex.msg);
		return 1;
	}

	// Check for arguments
	if (argc == 1) {
		usage();
		return 0;
	}


	CmdParse parser;
	parser.add("-d", "--device", true);
	parser.add("-t", "--threads", true);
	parser.add("-b", "--blocks", true);
	parser.add("-p", "--points", true);
	parser.add("-d", "--device", true);
	parser.add("-c", "--compressed", false);
	parser.add("-u", "--uncompressed", false);
	parser.add("", "--compression", true);
	parser.add("-i", "--in", true);
	parser.add("-o", "--out", true);
	parser.add("-f", "--follow", false);
	parser.add("-m", "--mode", true);
	parser.add("", "--list-devices", false);
	parser.add("", "--keyspace", true);
	parser.add("", "--continue", true);
	parser.add("", "--share", true);
	parser.add("", "--stride", true);
	parser.add("", "--keymask", true);
	parser.add("", "--rstride", true);
	parser.add("", "--skip", true);
	parser.add("", "--keyspaceoverride", true);
	parser.add("", "--netsize", true);
	parser.add("", "--randomfill", true);
	parser.add("", "--maxhrs", true);
	parser.add("", "--maxseconds", true);
	parser.add("", "--maxcycles", true);
	parser.add("", "--autostrides", true);
	parser.add("", "--stridemap", true);
	parser.add("-v", "--version", false);


	try {
		parser.parse(argc, argv);
	}
	catch (std::string err) {
		Logger::log(LogLevel::Error, "Error: " + err);
		return 1;
	}

	std::vector<OptArg> args = parser.getArgs();

	for (unsigned int i = 0; i < args.size(); i++) {
		OptArg optArg = args[i];
		std::string opt = args[i].option;

		try {
			if (optArg.equals("-t", "--threads")) {
				_config.threads = util::parseUInt32(optArg.arg);
				optThreads = true;
			}
			else if (optArg.equals("-b", "--blocks")) {
				_config.blocks = util::parseUInt32(optArg.arg);
				optBlocks = true;
			}
			else if (optArg.equals("-p", "--points")) {
				_config.pointsPerThread = util::parseUInt32(optArg.arg);
				optPoints = true;
			}
			else if (optArg.equals("-d", "--device")) {
				_config.device = util::parseUInt32(optArg.arg);
			}
			else if (optArg.equals("-c", "--compressed")) {
				optCompressed = true;
			}
			else if (optArg.equals("-u", "--uncompressed")) {
				optUncompressed = true;
			}
			else if (optArg.equals("-v", "--version")) {
				printf("BitCrack v" RELEASE "\n");
				return 0;
			}
			else if (optArg.equals("", "--compression")) {
				_config.compression = parseCompressionString(optArg.arg);
			}
			else if (optArg.equals("-m", "--mode")) {
				_config.searchMode = parseSearchModeString(optArg.arg);
			}
			else if (optArg.equals("-i", "--in")) {
				_config.targetsFile = optArg.arg;
			}
			else if (optArg.equals("-o", "--out")) {
				_config.resultsFile = optArg.arg;
			}
			else if (optArg.equals("", "--list-devices")) {
				listDevices = true;
			}
			else if (optArg.equals("", "--keyspace")) {
				secp256k1::uint256 start;
				secp256k1::uint256 end;

				parseKeyspace(optArg.arg, start, end);

				if (start.cmp(secp256k1::N) > 0) {
					throw std::string("argument is out of range");
				}
				if (start.isZero()) {
					throw std::string("argument is out of range");
				}

				if (end.cmp(secp256k1::N) > 0) {
					throw std::string("argument is out of range");
				}

				if (start.cmp(end) > 0) {
					throw std::string("Invalid argument");
				}

				_config.startKey = start;
				_config.nextKey = start;
				_config.endKey = end;
			}
			else if (optArg.equals("", "--stride")) {
				try {
					_config.stride = secp256k1::uint256(optArg.arg);
				}
				catch (...) {
					throw std::string("invalid argument: : expected hex string");
				}

				if (_config.stride.cmp(secp256k1::N) >= 0) {
					throw std::string("argument is out of range");
				}

				if (_config.stride.cmp(0) == 0) {
					throw std::string("argument is out of range");
				}
			}
			else if (optArg.equals("", "--stridemap")) {
				_config.strideMapFile = optArg.arg;
				if (_config.strideMapFile.size() > 0) _config.continueAfterEnd = true;
			}
			else if (optArg.equals("", "--keymask")) {
				try {
					_config.keyMask = optArg.arg;
				}
				catch (...) {
					throw std::string("invalid argument: : expected hex string");
				}
			}
			else if (optArg.equals("-f", "--follow")) {
				_config.follow = true;
			}
			else if (optArg.equals("", "--rstride")) {
				_config.randomSrtrideBits = util::parseUInt32(optArg.arg);
				if (_config.randomSrtrideBits > 1) {
					_config.randomStride = true;
					_config.continueAfterEnd = true;
				}
			}
			else if (optArg.equals("", "--maxhrs")) {
				_config.maxHours = util::parseUInt64(optArg.arg);
			}
			else if (optArg.equals("", "--maxseconds")) {
				_config.maxSeconds = util::parseUInt64(optArg.arg);
			}
			else if (optArg.equals("", "--maxcycles")) {
				_config.maxCycles = util::parseUInt64(optArg.arg);
			}
			else if (optArg.equals("", "--autostrides")) {
				_config.autoStrides = util::parseUInt64(optArg.arg);
				if (_config.autoStrides > 0) {
					_config.continueAfterEnd = true;
				}
			}
			else if (optArg.equals("", "--skip")) {
				_config.skip = secp256k1::uint256(optArg.arg);
			}
			else if (optArg.equals("", "--randomfill")) {
				_config.randFill = util::parseUInt32(optArg.arg);
			}
			else if (optArg.equals("", "--netsize")) {
				_config.netSize = util::parseUInt32(optArg.arg);
			}
		}
		catch (std::string err) {
			Logger::log(LogLevel::Error, "Error " + opt + ": " + err);
			return 1;
		}
	}

	//if (_config.continueAfterEnd && !_config.randomStride) {
	//	Logger::log(LogLevel::Error, "Can't use --conte without --rs, check -h for more details.");
	//	return 1;
	//}


	if (listDevices) {
		printDeviceList(_devices);
		return 0;
	}

	// Verify device exists
	if (_config.device < 0 || _config.device >= _devices.size()) {
		Logger::log(LogLevel::Error, "device " + util::format(_config.device) + " does not exist");
		return 1;
	}

	// Parse operands
	std::vector<std::string> ops = parser.getOperands();

	// If there are no operands, then we must be reading from a file, otherwise
	if (_config.searchMode == SearchMode::ADDRESS) {
		// expect addresses on the commandline
		if (ops.size() == 0) {
			if (_config.targetsFile.length() == 0) {
				Logger::log(LogLevel::Error, "Missing arguments");
				usage();
				return 1;
			}
		}
		else {
			for (unsigned int i = 0; i < ops.size(); i++) {
				if (!Address::verifyAddress(ops[i])) {
					Logger::log(LogLevel::Error, "Invalid address '" + ops[i] + "'");
					return 1;
				}
				_config.targets.push_back(ops[i]);
			}
		}
	}
	else {
		// expect xpoints on the commandline
		if (ops.size() == 0) {
			if (_config.targetsFile.length() == 0) {
				Logger::log(LogLevel::Error, "Missing arguments");
				usage();
				return 1;
			}
		}
		else {
			for (unsigned int i = 0; i < ops.size(); i++) {
				if (!XPoint::verifyXPoint(ops[i])) {
					Logger::log(LogLevel::Error, "Invalid xpoint '" + ops[i] + "'");
					return 1;
				}
				_config.targets.push_back(ops[i]);
			}
		}
	}


	// Check option for compressed, uncompressed, or both
	if (optCompressed && optUncompressed) {
		_config.compression = PointCompressionType::BOTH;
	}
	else if (optCompressed) {
		_config.compression = PointCompressionType::COMPRESSED;
	}
	else if (optUncompressed) {
		_config.compression = PointCompressionType::UNCOMPRESSED;
	}


	if (_config.searchMode == SearchMode::XPOINT && _config.compression != PointCompressionType::COMPRESSED) {
		Logger::log(LogLevel::Error, "'XPOINT' search mode not valid with compression '" + getCompressionString(_config.compression) + "'");
		return 1;
	}

	return run();
}