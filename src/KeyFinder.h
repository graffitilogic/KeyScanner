#ifndef KFINDERH
#define KFINDERH

#include <string>
#include <vector>
//#include "SECP256k1.h"
#include "search/Bloom.h"
#include "gpu/GPUEngine.h"
#include "rng/GPURand.h"


#ifdef WIN64
#include <Windows.h>
#endif

#define CPU_GRP_SIZE (1024*2)

class KeyFinder;

typedef struct {
	KeyFinder* obj;
	int  threadId;
	bool isRunning;
	bool hasStarted;

	int  gridSizeX;
	int  gridSizeY;
	int  gpuId;

	Int rangeStart;
	Int rangeEnd;
	bool rKeyRequest;
} TH_PARAM;


class KeyFinder
{

public:

	KeyFinder(const std::string& inputFile, int compMode, int searchMode, int coinType, bool useGpu, 
		const std::string& outputFile, bool useSSE, uint32_t maxFound, uint64_t rKey, int nbit2, int next, int zet, int display,
		const std::string& rangeStart, const std::string& rangeEnd, bool& should_exit);

	KeyFinder(const std::vector<unsigned char>& hashORxpoint, int compMode, int searchMode, int coinType, 
		bool useGpu, const std::string& outputFile, bool useSSE, uint32_t maxFound, uint64_t rKey, int nbit2, int next, int zet, int display,
		const std::string& rangeStart, const std::string& rangeEnd, bool& should_exit);

	~KeyFinder();

	void Search(int nbThread, std::vector<int> gpuId, std::vector<int> gridSize, bool& should_exit);
	void FindKeyCPU(TH_PARAM* p);

	void FindKeyGPU(TH_PARAM* p);
	void FindKeyGPUThrust(TH_PARAM* ph);
	void FindKeyGPUX(TH_PARAM* p);

private:

	//std::vector<std::vector<unsigned int>> mRandomizers;

	void InitGenratorTable();

	std::string GetHex(std::vector<unsigned char>& buffer);
	bool checkPrivKey(std::string addr, Int& key, int32_t incr, bool mode);
	bool checkPrivKeyETH(std::string addr, Int& key, int32_t incr);
	bool checkPrivKeyX(Int& key, int32_t incr, bool mode);

	void checkMultiAddresses(bool compressed, Int key, int i, Point p1);
	void checkMultiAddressesETH(Int key, int i, Point p1);
	void checkSingleAddress(bool compressed, Int key, int i, Point p1);
	void checkSingleAddressETH(Int key, int i, Point p1);
	void checkMultiXPoints(bool compressed, Int key, int i, Point p1);
	void checkSingleXPoint(bool compressed, Int key, int i, Point p1);

	void checkMultiAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4);
	void checkSingleAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4);

	void output(std::string addr, std::string pAddr, std::string pAddrHex, std::string pubKey);
	bool isAlive(TH_PARAM* p);

	bool hasStarted(TH_PARAM* p);
	uint64_t getGPUCount();
	uint64_t getCPUCount();
	void rKeyRequest(TH_PARAM* p);
	void SetupRanges(uint32_t totalThreads);

	void getCPUStartingKey(Int& tRangeStart, Int& tRangeEnd, Int& key, Point& startP);
	
	std::vector<Int> KeyFinder::getRandomIncrementors(Int model);
//	std::vector<Int> KeyFinder::getIncrementorKeyModel(std::vector<Int> incrementors, std::vector<Int> keys);


	std::vector<Int> KeyFinder::getGPUAssistedRandoms(Int min, Int max, uint64_t length);
	std::vector<Int> KeyFinder::getGPUAssistedRandoms32(Int min, Int max, uint64_t length);
	
	std::vector<Int> KeyFinder::getGPURandomsED(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer);
	std::vector<Int> KeyFinder::getGPURandomsED2(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer);
	std::vector<Int> KeyFinder::getGPURandomsED3(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer);
	std::vector<Int> KeyFinder::getGPURandomsED4(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer);


	std::vector<Int> KeyFinder::getGPUAssistedMaskedRandoms(Random::GPURand& rndGPU, Int min, Int max, uint64_t length);
	std::vector<Int> KeyFinder::getGPUAssistedMaskedRandoms(Int min, Int max, uint64_t length);

	void getGPUStartingKeys(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);
	void getGPUStartingKeys2(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);
	void getGPUStartingKeys3(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);

	void getEvenlyDistributedGPUStartingKeys(Random::GPURand& rndGPU, Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);

	void getGPUStartingKeysViaThrust(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);
	void getGPUStartingKeysViaThrust(Random::GPURand& rndGPU, Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p);

	void getGPUStartingKeysAndParts(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, std::vector<std::vector<Int>>* keyParts, Point* p);
	void getGPUStartingKeysWithParts(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, std::vector<std::vector<Int>>* keyParts, Point* p);

	int CheckBloomBinary(const uint8_t* _xx, uint32_t K_LENGTH);
	bool MatchHash(uint32_t* _h);
	bool MatchXPoint(uint32_t* _h);
	std::string formatThousands(uint64_t x);
	char* toTimeStr(int sec, char* timeStr);

	Secp256K1* secp;
	Bloom* bloom;

	uint64_t counters[256];
	double startTime;

	int compMode;
	int searchMode;
	int coinType;

	bool useGpu;
	bool endOfSearch;
	int nbCPUThread;
	int nbGPUThread;
	int nbFoundKey;
	int incrementor_iterations = 0;
	int prefix_cycles = 0;

	uint64_t targetCounter;
	std::string stroka;
	std::string outputFile;
	std::string inputFile;
	uint32_t hash160Keccak[5];
	uint32_t xpoint[8];
	bool useSSE;
	Int rangeStart8;
	Int rangeStart;
	Int rangeEnd;
	Int rangeDiff;
	Int rangeDiff2;
	Int rangeDiffcp;
	Int rangeDiffbar;
	Int gir;
	int trk = 0;
	Int razn;
	Int rhex;
	int minuty;
	int zhdat;
	uint64_t value777;
	uint32_t maxFound;
	uint64_t rKey;
	int next;
	int zet;
	int display;
	int nbit2;
	int gpucores;
	uint64_t lastrKey;
	uint64_t rKeyCount2;
	int corres;
	uint8_t* DATA;
	uint64_t TOTAL_COUNT;
	uint64_t BLOOM_N;

#ifdef WIN64
	HANDLE ghMutex;
#else
	pthread_mutex_t  ghMutex;
#endif

};

#endif //end header for keyfinder
