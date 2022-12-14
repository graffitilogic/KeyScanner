#include "KeyFinder.h"
#include "math/GmpUtil.h"
#include "hash/Base58.h"
#include "refactorme/sha256.cpp"
#include "hash/keccak160.h"
#include "math/IntGroup.h"
#include "rng/Timer.h"
#include "hash/ripemd160.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <stdio.h>
#include <time.h>
#ifndef WIN64
#include <pthread.h>
#endif
#include <chrono>

//#include <algorithm>

using namespace std;

Point Gn[CPU_GRP_SIZE / 2];
Point _2Gn;

// ----------------------------------------------------------------------------

KeyFinder::KeyFinder(const std::string& inputFile, int compMode, int searchMode, int coinType, bool useGpu,
	const std::string& outputFile, bool useSSE, uint32_t maxFound, uint64_t rKey, int nbit2, int next, int zet, int display,
	const std::string& rangeStart, const std::string& rangeEnd, bool& should_exit, int maxSeconds)
{
	this->compMode = compMode;
	this->useGpu = useGpu;
	this->outputFile = outputFile;
	this->useSSE = useSSE;
	this->nbGPUThread = 0;
	this->inputFile = inputFile;
	this->maxFound = maxFound;
	this->rKey = rKey;
	this->maxSeconds = maxSeconds;
	this->nbit2 = nbit2;
	this->next = next;
	this->zet = zet;
	this->display = display;
	this->stroka = stroka;
	this->searchMode = searchMode;
	this->coinType = coinType;
	this->rangeStart.SetBase16(rangeStart.c_str());
	this->rangeStart8.SetBase16(rangeStart.c_str());
	this->rhex.SetBase16(rangeStart.c_str());
	this->rangeEnd.SetBase16(rangeEnd.c_str());
	this->rangeDiff2.Set(&this->rangeEnd);
	this->rangeDiff2.Sub(&this->rangeStart);
	this->rangeDiffbar.Set(&this->rangeDiff2);
	this->rangeDiffcp.Set(&this->rangeDiff2);
	this->lastrKey = 0;

	secp = new Secp256K1();
	secp->Init();

	// load file
	FILE* wfd;
	uint64_t N = 0;

	wfd = fopen(this->inputFile.c_str(), "rb");
	if (!wfd) {
		printf("  %s can not open\n", this->inputFile.c_str());
		exit(1);
	}

#ifdef WIN64
	_fseeki64(wfd, 0, SEEK_END);
	N = _ftelli64(wfd);
#else
	fseek(wfd, 0, SEEK_END);
	N = ftell(wfd);
#endif

	int K_LENGTH = 20;
	if (this->searchMode == (int)SEARCH_MODE_MX)
		K_LENGTH = 32;

	N = N / K_LENGTH;
	rewind(wfd);

	DATA = (uint8_t*)malloc(N * K_LENGTH);
	memset(DATA, 0, N * K_LENGTH);

	uint8_t* buf = (uint8_t*)malloc(K_LENGTH);;

	bloom = new Bloom(2 * N, 0.000001);

	uint64_t percent = (N - 1) / 100;
	uint64_t i = 0;
	printf("\n");
	while (i < N && !should_exit) {
		memset(buf, 0, K_LENGTH);
		memset(DATA + (i * K_LENGTH), 0, K_LENGTH);
		if (fread(buf, 1, K_LENGTH, wfd) == K_LENGTH) {
			bloom->add(buf, K_LENGTH);
			memcpy(DATA + (i * K_LENGTH), buf, K_LENGTH);
			if ((percent != 0) && i % percent == 0) {
				printf("\r  Loading      : %llu %%", (i / percent));
				fflush(stdout);
			}
		}
		i++;
	}
	fclose(wfd);
	free(buf);

	if (should_exit) {
		delete secp;
		delete bloom;
		if (DATA)
			free(DATA);
		exit(0);
	}

	BLOOM_N = bloom->get_bytes();
	TOTAL_COUNT = N;
	targetCounter = i;
	if (coinType == COIN_BTC) {
		if (searchMode == (int)SEARCH_MODE_MA)
			printf("\n  Loaded       : %s Bitcoin addresses\n", formatThousands(i).c_str());
		else if (searchMode == (int)SEARCH_MODE_MX)
			printf("\n  Loaded       : %s Bitcoin xpoints\n", formatThousands(i).c_str());
	}
	else {
		printf("\n  Loaded       : %s Ethereum addresses\n", formatThousands(i).c_str());
	}

	printf("\n");

	bloom->print();
	printf("\n");

	InitGenratorTable();

}

// ----------------------------------------------------------------------------

KeyFinder::KeyFinder(const std::vector<unsigned char>& hashORxpoint, int compMode, int searchMode, int coinType,
	bool useGpu, const std::string& outputFile, bool useSSE, uint32_t maxFound, uint64_t rKey, int nbit2, int next, int zet, int display,
	const std::string& rangeStart, const std::string& rangeEnd, bool& should_exit, int maxSeconds)
{
	this->compMode = compMode;
	this->useGpu = useGpu;
	this->outputFile = outputFile;
	this->useSSE = useSSE;
	this->nbGPUThread = 0;
	this->maxFound = maxFound;
	this->rKey = rKey;
	this->maxSeconds = maxSeconds;
	this->next = next;
	this->zet = zet;
	this->display = display;
	this->stroka = stroka;
	this->searchMode = searchMode;
	this->coinType = coinType;
	this->rangeStart.SetBase16(rangeStart.c_str());
	this->rangeStart8.SetBase16(rangeStart.c_str());
	this->rhex.SetBase16(rangeStart.c_str());
	this->rangeEnd.SetBase16(rangeEnd.c_str());
	this->rangeDiff2.Set(&this->rangeEnd);
	this->rangeDiff2.Sub(&this->rangeStart);
	this->rangeDiffcp.Set(&this->rangeDiff2);
	this->rangeDiffbar.Set(&this->rangeDiff2);
	this->targetCounter = 1;
	this->nbit2 = nbit2;
	secp = new Secp256K1();
	secp->Init();

	if (this->searchMode == (int)SEARCH_MODE_SA) {
		assert(hashORxpoint.size() == 20);
		for (size_t i = 0; i < hashORxpoint.size(); i++) {
			((uint8_t*)hash160Keccak)[i] = hashORxpoint.at(i);
		}
	}
	else if (this->searchMode == (int)SEARCH_MODE_SX) {
		assert(hashORxpoint.size() == 32);
		for (size_t i = 0; i < hashORxpoint.size(); i++) {
			((uint8_t*)xpoint)[i] = hashORxpoint.at(i);
		}
	}
	printf("\n");

	InitGenratorTable();
}

// ----------------------------------------------------------------------------

void KeyFinder::InitGenratorTable()
{
	// Compute Generator table G[n] = (n+1)*G
	Point g = secp->G;
	Gn[0] = g;
	g = secp->DoubleDirect(g);
	Gn[1] = g;
	for (int i = 2; i < CPU_GRP_SIZE / 2; i++) {
		g = secp->AddDirect(g, secp->G);
		Gn[i] = g;
	}
	// _2Gn = CPU_GRP_SIZE*G
	_2Gn = secp->DoubleDirect(Gn[CPU_GRP_SIZE / 2 - 1]);

	char* ctimeBuff;
	time_t now = time(NULL);
	ctimeBuff = ctime(&now);
	printf("  Start Time   : %s", ctimeBuff);

	if (rKey < 1) {

		if (next > 0) {
			ifstream file777("KeyScanner_Continue.bat");
			string s777;
			string kogda;

			for (int i = 0; i < 5; i++) {
				getline(file777, s777);

				if (i == 1) {
					stroka = s777;
				}
				if (i == 3) {
					string kogda = s777;
					if (kogda != "") {
						printf("  Rotor        : Continuing search from BAT file. Checkpoint %s \n\n", kogda.c_str());
					}
				}
				if (i == 4) {
					string streek = s777;
					std::istringstream iss(streek);
					iss >> value777;
					uint64_t dobb = value777 / 1;
					rhex.Add(dobb);
				}
			}

			if (kogda == "") {
				ifstream file778("KeyScanner_START.bat");
				string s778;
				string kogda;
				stroka = "";
				for (int i = 0; i < 3; i++) {
					getline(file778, s778);

					if (i == 1) {
						stroka = s778;
					}
				}
			}
		}
	}
	if (display == 0) {

		if (rKey == 0) {
			
			if (nbit2 > 0) {
				Int tThreads77;
				tThreads77.SetInt32(nbit2);
				rangeDiffcp.Div(&tThreads77);

				gir.Set(&rangeDiff2);
				Int reh;
				uint64_t nextt99;
				nextt99 = value777 * 1;
				reh.Add(nextt99);
				gir.Sub(&reh);
				if (value777 > 1) {
					printf("\n  Rotor info   : Continuation... Divide the remaining range %s (%d bit) into CPU %d cores \n", gir.GetBase16().c_str(), gir.GetBitLength(), nbit2);
				}
			}
		}
	}
	else {

		if (rKey == 0) {
			printf("  Global start : %s (%d bit)\n", this->rangeStart.GetBase16().c_str(), this->rangeStart.GetBitLength());
			printf("  Global end   : %s (%d bit)\n", this->rangeEnd.GetBase16().c_str(), this->rangeEnd.GetBitLength());
			printf("  Global range : %s (%d bit)\n", this->rangeDiff2.GetBase16().c_str(), this->rangeDiff2.GetBitLength());

			if (nbit2 > 0) {
				Int tThreads77;
				tThreads77.SetInt32(nbit2);
				rangeDiffcp.Div(&tThreads77);

				gir.Set(&rangeDiff2);
				Int reh;
				uint64_t nextt99;
				nextt99 = value777 * 1;
				reh.Add(nextt99);
				gir.Sub(&reh);
				if (value777 > 1) {
					printf("\n  Rotor info   : Continuation... Divide the remaining range %s (%d bit) into CPU %d cores \n", gir.GetBase16().c_str(), gir.GetBitLength(), nbit2);
				}
				else {
					printf("\n  Rotor info   : Divide the range %s (%d bit) into CPU %d cores for fast parallel search \n", rangeDiff2.GetBase16().c_str(), rangeDiff2.GetBitLength(), nbit2);
				}
			}
		}
	
	}
	
}

// ----------------------------------------------------------------------------

KeyFinder::~KeyFinder()
{
	delete secp;
	if (searchMode == (int)SEARCH_MODE_MA || searchMode == (int)SEARCH_MODE_MX)
		delete bloom;
	if (DATA)
		free(DATA);
}

// ----------------------------------------------------------------------------

double log1(double x)
{
	// Use taylor series to approximate log(1-x)
	return -x - (x * x) / 2.0 - (x * x * x) / 3.0 - (x * x * x * x) / 4.0;
}

void KeyFinder::output(std::string addr, std::string pAddr, std::string pAddrHex, std::string pubKey)
{

#ifdef WIN64
	WaitForSingleObject(ghMutex, INFINITE);
#else
	pthread_mutex_lock(&ghMutex);
#endif

	FILE* f = stdout;
	bool needToClose = false;

	if (outputFile.length() > 0) {
		f = fopen(outputFile.c_str(), "a");
		if (f == NULL) {
			printf("  Cannot open %s for writing\n", outputFile.c_str());
			f = stdout;
		}
		else {
			needToClose = true;
		}
	}

	if (!needToClose)
		printf("\n");
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	fprintf(f, "PubAddress: %s\n", addr.c_str());
	fprintf(stdout, "\n  =================================================================================\n");
	fprintf(stdout, "  PubAddress: %s\n", addr.c_str());

	if (coinType == COIN_BTC) {
		fprintf(f, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		fprintf(stdout, "  Priv (WIF): p2pkh:%s\n", pAddr.c_str());
	}

	fprintf(f, "Priv (HEX): %s\n", pAddrHex.c_str());
	fprintf(stdout, "  Priv (HEX): %s\n", pAddrHex.c_str());

	fprintf(f, "PubK (HEX): %s\n", pubKey.c_str());
	fprintf(stdout, "  PubK (HEX): %s\n", pubKey.c_str());

	fprintf(f, "=================================================================================\n");
	fprintf(stdout, "  =================================================================================\n");

	if (needToClose)
		fclose(f);

#ifdef WIN64
	ReleaseMutex(ghMutex);
#else
	pthread_mutex_unlock(&ghMutex);
#endif

}

// ----------------------------------------------------------------------------

bool KeyFinder::checkPrivKey(std::string addr, Int& key, int32_t incr, bool mode)
{
	Int k(&key), k2(&key);
	k.Add((uint64_t)incr);
	k2.Add((uint64_t)incr);
	// Check addresses
	Point p = secp->ComputePublicKey(&k);
	std::string px = p.x.GetBase16();
	std::string chkAddr = secp->GetAddress(mode, p);
	if (chkAddr != addr) {
		//Key may be the opposite one (negative zero or compressed key)
		k.Neg();
		k.Add(&secp->order);
		p = secp->ComputePublicKey(&k);
		std::string chkAddr = secp->GetAddress(mode, p);
		if (chkAddr != addr) {
			printf("\n=================================================================================\n");
			printf("  Warning, wrong private key generated !\n");
			printf("  PivK : %s\n", k2.GetBase16().c_str());
			printf("  Addr : %s\n", addr.c_str());
			printf("  PubX : %s\n", px.c_str());
			printf("  PivK : %s\n", k.GetBase16().c_str());
			printf("  Check: %s\n", chkAddr.c_str());
			printf("  PubX : %s\n", p.x.GetBase16().c_str());
			printf("=================================================================================\n");
			return false;
		}
	}
	output(addr, secp->GetPrivAddress(mode, k), k.GetBase16(), secp->GetPublicKeyHex(mode, p));
	return true;
}

bool KeyFinder::checkPrivKeyETH(std::string addr, Int& key, int32_t incr)
{
	Int k(&key), k2(&key);
	k.Add((uint64_t)incr);
	k2.Add((uint64_t)incr);
	// Check addresses
	Point p = secp->ComputePublicKey(&k);
	std::string px = p.x.GetBase16();
	std::string chkAddr = secp->GetAddressETH(p);
	if (chkAddr != addr) {
		//Key may be the opposite one (negative zero or compressed key)
		k.Neg();
		k.Add(&secp->order);
		p = secp->ComputePublicKey(&k);
		std::string chkAddr = secp->GetAddressETH(p);
		if (chkAddr != addr) {
			printf("\n=================================================================================\n");
			printf("  Warning, wrong private key generated !\n");
			printf("  PivK :%s\n", k2.GetBase16().c_str());
			printf("  Addr :%s\n", addr.c_str());
			printf("  PubX :%s\n", px.c_str());
			printf("  PivK :%s\n", k.GetBase16().c_str());
			printf("  Check:%s\n", chkAddr.c_str());
			printf("  PubX :%s\n", p.x.GetBase16().c_str());
			printf("=================================================================================\n");
			return false;
		}
	}
	output(addr, k.GetBase16()/*secp->GetPrivAddressETH(k)*/, k.GetBase16(), secp->GetPublicKeyHexETH(p));
	return true;
}

bool KeyFinder::checkPrivKeyX(Int& key, int32_t incr, bool mode)
{
	Int k(&key);
	k.Add((uint64_t)incr);
	Point p = secp->ComputePublicKey(&k);
	std::string addr = secp->GetAddress(mode, p);
	output(addr, secp->GetPrivAddress(mode, k), k.GetBase16(), secp->GetPublicKeyHex(mode, p));
	return true;
}

// ----------------------------------------------------------------------------

#ifdef WIN64
DWORD WINAPI _FindKeyCPU(LPVOID lpParam)
{
#else
void* _FindKeyCPU(void* lpParam)
{
#endif
	TH_PARAM* p = (TH_PARAM*)lpParam;
	p->obj->FindKeyCPU(p);
	return 0;
}

#ifdef WIN64
DWORD WINAPI _FindKeyGPU(LPVOID lpParam)
{
#else
void* _FindKeyGPU(void* lpParam)
{
#endif
	TH_PARAM* p = (TH_PARAM*)lpParam;
	//p->obj->FindKeyGPUX(p);
	p->obj->FindKeyGPUThrust(p);
	return 0;
}

// ----------------------------------------------------------------------------

void KeyFinder::checkMultiAddresses(bool compressed, Int key, int i, Point p1)
{
	unsigned char h0[20];

	// Point
	secp->GetHash160(compressed, p1, h0);
	if (CheckBloomBinary(h0, 20) > 0) {
		std::string addr = secp->GetAddress(compressed, h0);
		if (checkPrivKey(addr, key, i, compressed)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkMultiAddressesETH(Int key, int i, Point p1)
{
	unsigned char h0[20];

	// Point
	secp->GetHashETH(p1, h0);
	if (CheckBloomBinary(h0, 20) > 0) {
		std::string addr = secp->GetAddressETH(h0);
		if (checkPrivKeyETH(addr, key, i)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkSingleAddress(bool compressed, Int key, int i, Point p1)
{
	unsigned char h0[20];

	// Point
	secp->GetHash160(compressed, p1, h0);
	if (MatchHash((uint32_t*)h0)) {
		std::string addr = secp->GetAddress(compressed, h0);
		if (checkPrivKey(addr, key, i, compressed)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkSingleAddressETH(Int key, int i, Point p1)
{
	unsigned char h0[20];

	// Point
	secp->GetHashETH(p1, h0);
	if (MatchHash((uint32_t*)h0)) {
		std::string addr = secp->GetAddressETH(h0);
		if (checkPrivKeyETH(addr, key, i)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkMultiXPoints(bool compressed, Int key, int i, Point p1)
{
	unsigned char h0[32];

	// Point
	secp->GetXBytes(compressed, p1, h0);
	if (CheckBloomBinary(h0, 32) > 0) {
		if (checkPrivKeyX(key, i, compressed)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkSingleXPoint(bool compressed, Int key, int i, Point p1)
{
	unsigned char h0[32];

	// Point
	secp->GetXBytes(compressed, p1, h0);
	if (MatchXPoint((uint32_t*)h0)) {
		if (checkPrivKeyX(key, i, compressed)) {
			nbFoundKey++;
		}
	}
}

// ----------------------------------------------------------------------------

void KeyFinder::checkMultiAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4)
{
	unsigned char h0[20];
	unsigned char h1[20];
	unsigned char h2[20];
	unsigned char h3[20];

	// Point -------------------------------------------------------------------------
	secp->GetHash160(compressed, p1, p2, p3, p4, h0, h1, h2, h3);
	if (CheckBloomBinary(h0, 20) > 0) {
		std::string addr = secp->GetAddress(compressed, h0);
		if (checkPrivKey(addr, key, i + 0, compressed)) {
			nbFoundKey++;
		}
	}
	if (CheckBloomBinary(h1, 20) > 0) {
		std::string addr = secp->GetAddress(compressed, h1);
		if (checkPrivKey(addr, key, i + 1, compressed)) {
			nbFoundKey++;
		}
	}
	if (CheckBloomBinary(h2, 20) > 0) {
		std::string addr = secp->GetAddress(compressed, h2);
		if (checkPrivKey(addr, key, i + 2, compressed)) {
			nbFoundKey++;
		}
	}
	if (CheckBloomBinary(h3, 20) > 0) {
		std::string addr = secp->GetAddress(compressed, h3);
		if (checkPrivKey(addr, key, i + 3, compressed)) {
			nbFoundKey++;
		}
	}

}

// ----------------------------------------------------------------------------

void KeyFinder::checkSingleAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4)
{
	unsigned char h0[20];
	unsigned char h1[20];
	unsigned char h2[20];
	unsigned char h3[20];

	// Point -------------------------------------------------------------------------
	secp->GetHash160(compressed, p1, p2, p3, p4, h0, h1, h2, h3);
	if (MatchHash((uint32_t*)h0)) {
		std::string addr = secp->GetAddress(compressed, h0);
		if (checkPrivKey(addr, key, i + 0, compressed)) {
			nbFoundKey++;
		}
	}
	if (MatchHash((uint32_t*)h1)) {
		std::string addr = secp->GetAddress(compressed, h1);
		if (checkPrivKey(addr, key, i + 1, compressed)) {
			nbFoundKey++;
		}
	}
	if (MatchHash((uint32_t*)h2)) {
		std::string addr = secp->GetAddress(compressed, h2);
		if (checkPrivKey(addr, key, i + 2, compressed)) {
			nbFoundKey++;
		}
	}
	if (MatchHash((uint32_t*)h3)) {
		std::string addr = secp->GetAddress(compressed, h3);
		if (checkPrivKey(addr, key, i + 3, compressed)) {
			nbFoundKey++;
		}
	}

}

// ----------------------------------------------------------------------------

void KeyFinder::getCPUStartingKey(Int & tRangeStart, Int & tRangeEnd, Int & key, Point & startP)
{
	if (rKey <= 0) {

		uint64_t nextt = 0;
		if (value777 > 1) {
			nextt = value777 / nbit2;
			tRangeStart.Add(nextt);
		}
		key.Set(&tRangeStart);
		Int kon;
		kon.Set(&tRangeStart);
		kon.Add(&rangeDiffcp);
		trk = trk + 1;
		if (display > 0) {

			if (trk == nbit2) {
				printf("  CPU Core (%d) : %s -> %s \n\n", trk, key.GetBase16().c_str(), rangeEnd.GetBase16().c_str());
			}
			else {
				printf("  CPU Core (%d) : %s -> %s \n", trk, key.GetBase16().c_str(), kon.GetBase16().c_str());
			}
		}
		
		Int km(&key);
		km.Add((uint64_t)CPU_GRP_SIZE / 2);
		startP = secp->ComputePublicKey(&km);
	}
	else {
		
		if (rangeDiff2.GetBitLength() > 1) {
	
			key.Rand2(&rangeStart8, &rangeEnd);
			rhex = key;
			Int km(&key);
			km.Add((uint64_t)CPU_GRP_SIZE / 2);
			startP = secp->ComputePublicKey(&km);
		}
		else {

			if (next > 256) {
				printf("\n  ROTOR Random : Are you serious %d bit ??? \n", next);
				exit(1);
			}
			if (zet > 256) {
				printf("\n  ROTOR Random : Are you serious -z %d bit ??? \n", zet);
				exit(1);
			}

			if (next == 0) {
				key.Rand(256);
				rhex = key;
				Int km(&key);
				km.Add((uint64_t)CPU_GRP_SIZE / 2);
				startP = secp->ComputePublicKey(&km);
			}
			else {

				if (zet > 1) {

					if (zet <= next) {
						printf("\n  ROTOR Random : Are you serious -n %d (start) -z %d (end) ??? \n  The start must be less than the end \n", next, zet);
						exit(1);
					}
					int dfs = zet - next;
					srand(time(NULL));
					int next3 = next + rand() % dfs;
					int next2 = next3 + rand() % 2;
					key.Rand(next2);
					rhex = key;
					Int km(&key);
					km.Add((uint64_t)CPU_GRP_SIZE / 2);
					startP = secp->ComputePublicKey(&km);
					
				}
				else {

					key.Rand(next);
					rhex = key;
					Int km(&key);
					km.Add((uint64_t)CPU_GRP_SIZE / 2);
					startP = secp->ComputePublicKey(&km);
				}
			}
		}
	}
}

void KeyFinder::FindKeyCPU(TH_PARAM * ph)
{
	
	// Global init
	int thId = ph->threadId;
	
	Int tRangeStart = ph->rangeStart;
	Int tRangeEnd = ph->rangeEnd;
	counters[thId] = 0;
	if (rKey < 1) {

		if (thId == 0) {
			
			if (display > 0) {
				
				if (next > 0) {
					printf("  info   : Save checkpoints every %d minutes. For continue range, run the bat file Rotor-Cuda_Continue.bat \n", next);
				}

			}
		}
	}
	
	if (rKey > 0) {

		if (rKeyCount2 == 0) {
			
			if (thId == 0) {

				if (rangeDiff2.GetBitLength() < 1) {

					if (next == 0) {

						if (display > 0) {
							printf("  info   : Generate Private keys in Ranges 95%% (252-256) bit + 5%% (248-252) bit \n");
						}
					}
					else {

						if (zet > 1) {
						
							if (display > 0) {
								printf("  info   : Private keys random %d <~> %d (bit) \n", next, zet);
							}
						}
						else {

							if (display > 0) {
								printf("  info   : Private keys random %d (bit)  \n", next); 
								
							}
						}
					}
				}
                else {

				    if (display > 0) {
						printf("  info   : Min %d (bit) %s \n", rangeStart8.GetBitLength(), rangeStart8.GetBase16().c_str());
						printf("  info   : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				    }
                }
				if (display > 0) {
					printf("  Base Key     : Randomly changes %d Private keys every %llu,000,000,000 on the counter\n\n", nbCPUThread, rKey);
				}
			}
		}
	}
	
	
	// CPU Thread
	IntGroup* grp = new IntGroup(CPU_GRP_SIZE / 2 + 1);

	// Group Init
	Int key;// = new Int();
	Point startP;// = new Point();
	getCPUStartingKey(tRangeStart, tRangeEnd, key, startP);

	Int* dx = new Int[CPU_GRP_SIZE / 2 + 1];
	Point* pts = new Point[CPU_GRP_SIZE];

	Int* dy = new Int();
	Int* dyn = new Int();
	Int* _s = new Int();
	Int* _p = new Int();
	Point* pp = new Point();
	Point* pn = new Point();
	grp->Set(dx);

	ph->hasStarted = true;
	ph->rKeyRequest = false;
	
	while (!endOfSearch) {

		if (ph->rKeyRequest) {
			getCPUStartingKey(tRangeStart, tRangeEnd, key, startP);
			ph->rKeyRequest = false;
		}

		// Fill group
		int i;
		int hLength = (CPU_GRP_SIZE / 2 - 1);

		for (i = 0; i < hLength; i++) {
			dx[i].ModSub(&Gn[i].x, &startP.x);
		}
		dx[i].ModSub(&Gn[i].x, &startP.x);  // For the first point
		dx[i + 1].ModSub(&_2Gn.x, &startP.x); // For the next center point

		// Grouped ModInv
		grp->ModInv();

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group

		// center point
		pts[CPU_GRP_SIZE / 2] = startP;

		for (i = 0; i < hLength && !endOfSearch; i++) {

			*pp = startP;
			*pn = startP;

			// P = startP + i*G
			dy->ModSub(&Gn[i].y, &pp->y);

			_s->ModMulK1(dy, &dx[i]);       // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p->ModSquareK1(_s);            // _p = pow2(s)

			pp->x.ModNeg();
			pp->x.ModAdd(_p);
			pp->x.ModSub(&Gn[i].x);           // rx = pow2(s) - p1.x - p2.x;

			pp->y.ModSub(&Gn[i].x, &pp->x);
			pp->y.ModMulK1(_s);
			pp->y.ModSub(&Gn[i].y);           // ry = - p2.y - s*(ret.x-p2.x);

			// P = startP - i*G  , if (x,y) = i*G then (x,-y) = -i*G
			dyn->Set(&Gn[i].y);
			dyn->ModNeg();
			dyn->ModSub(&pn->y);

			_s->ModMulK1(dyn, &dx[i]);      // s = (p2.y-p1.y)*inverse(p2.x-p1.x);
			_p->ModSquareK1(_s);            // _p = pow2(s)

			pn->x.ModNeg();
			pn->x.ModAdd(_p);
			pn->x.ModSub(&Gn[i].x);          // rx = pow2(s) - p1.x - p2.x;

			pn->y.ModSub(&Gn[i].x, &pn->x);
			pn->y.ModMulK1(_s);
			pn->y.ModAdd(&Gn[i].y);          // ry = - p2.y - s*(ret.x-p2.x);

			pts[CPU_GRP_SIZE / 2 + (i + 1)] = *pp;
			pts[CPU_GRP_SIZE / 2 - (i + 1)] = *pn;

		}

		// First point (startP - (GRP_SZIE/2)*G)
		*pn = startP;
		dyn->Set(&Gn[i].y);
		dyn->ModNeg();
		dyn->ModSub(&pn->y);

		_s->ModMulK1(dyn, &dx[i]);
		_p->ModSquareK1(_s);

		pn->x.ModNeg();
		pn->x.ModAdd(_p);
		pn->x.ModSub(&Gn[i].x);

		pn->y.ModSub(&Gn[i].x, &pn->x);
		pn->y.ModMulK1(_s);
		pn->y.ModAdd(&Gn[i].y);

		pts[0] = *pn;

		// Next start point (startP + GRP_SIZE*G)
		*pp = startP;
		dy->ModSub(&_2Gn.y, &pp->y);

		_s->ModMulK1(dy, &dx[i + 1]);
		_p->ModSquareK1(_s);

		pp->x.ModNeg();
		pp->x.ModAdd(_p);
		pp->x.ModSub(&_2Gn.x);

		pp->y.ModSub(&_2Gn.x, &pp->x);
		pp->y.ModMulK1(_s);
		pp->y.ModSub(&_2Gn.y);
		startP = *pp;

		// Check addresses
		if (useSSE) {
			for (int i = 0; i < CPU_GRP_SIZE && !endOfSearch; i += 4) {
				switch (compMode) {
				case SEARCH_COMPRESSED:
					if (searchMode == (int)SEARCH_MODE_MA) {
						checkMultiAddressesSSE(true, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					else if (searchMode == (int)SEARCH_MODE_SA) {
						checkSingleAddressesSSE(true, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					break;
				case SEARCH_UNCOMPRESSED:
					if (searchMode == (int)SEARCH_MODE_MA) {
						checkMultiAddressesSSE(false, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					else if (searchMode == (int)SEARCH_MODE_SA) {
						checkSingleAddressesSSE(false, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					break;
				case SEARCH_BOTH:
					if (searchMode == (int)SEARCH_MODE_MA) {
						checkMultiAddressesSSE(true, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
						checkMultiAddressesSSE(false, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					else if (searchMode == (int)SEARCH_MODE_SA) {
						checkSingleAddressesSSE(true, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
						checkSingleAddressesSSE(false, key, i, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
					}
					break;
				}
			}
		}
		else {
			if (coinType == COIN_BTC) {
				for (int i = 0; i < CPU_GRP_SIZE && !endOfSearch; i++) {
					switch (compMode) {
					case SEARCH_COMPRESSED:
						switch (searchMode) {
						case (int)SEARCH_MODE_MA:
							checkMultiAddresses(true, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SA:
							checkSingleAddress(true, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_MX:
							checkMultiXPoints(true, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SX:
							checkSingleXPoint(true, key, i, pts[i]);
							break;
						default:
							break;
						}
						break;
					case SEARCH_UNCOMPRESSED:
						switch (searchMode) {
						case (int)SEARCH_MODE_MA:
							checkMultiAddresses(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SA:
							checkSingleAddress(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_MX:
							checkMultiXPoints(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SX:
							checkSingleXPoint(false, key, i, pts[i]);
							break;
						default:
							break;
						}
						break;
					case SEARCH_BOTH:
						switch (searchMode) {
						case (int)SEARCH_MODE_MA:
							checkMultiAddresses(true, key, i, pts[i]);
							checkMultiAddresses(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SA:
							checkSingleAddress(true, key, i, pts[i]);
							checkSingleAddress(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_MX:
							checkMultiXPoints(true, key, i, pts[i]);
							checkMultiXPoints(false, key, i, pts[i]);
							break;
						case (int)SEARCH_MODE_SX:
							checkSingleXPoint(true, key, i, pts[i]);
							checkSingleXPoint(false, key, i, pts[i]);
							break;
						default:
							break;
						}
						break;
					}
				}
			}
			else {
				for (int i = 0; i < CPU_GRP_SIZE && !endOfSearch; i++) {
					switch (searchMode) {
					case (int)SEARCH_MODE_MA:
						checkMultiAddressesETH(key, i, pts[i]);
						break;
					case (int)SEARCH_MODE_SA:
						checkSingleAddressETH(key, i, pts[i]);
						break;
					default:
						break;
					}
				}
			}
		}
		key.Add((uint64_t)CPU_GRP_SIZE);
		counters[thId] += CPU_GRP_SIZE; // Point
	}
	ph->isRunning = false;

	delete grp;
	delete[] dx;
	delete[] pts;

	delete dy;
	delete dyn;
	delete _s;
	delete _p;
	delete pp;
	delete pn;
}

// ----------------------------------------------------------------------------

std::vector<Int> KeyFinder::getRandomIncrementors(Int model) {

	std::vector<Int> results;
	std::string sModel = model.GetBase16().c_str();
	int stepSize = 6;//2;// 4;
	bool isFinished = false;

	int padding = 0;

	while (!isFinished) {
		std::string s;
		int pad = 0;
		while (pad < padding) {
			s += "0";
			pad++;
			if (s.size() >= sModel.size()) break;
		}

		s = "1" + s;
		if (!s.empty()) {
			Int iterator;
			iterator.SetBase16(s.c_str());
			results.push_back(iterator);
		}

		padding += stepSize;

		if (s.size() >= sModel.size()) isFinished = true;

	}

	results.insert(results.begin(), Int((uint64_t)0));
	for (int i = 0; i < 2; i++) {
		results.pop_back();
	}

	

	results.clear();

	//undo this for now
	std::string s = "1";
	Int iterator;
	iterator.SetBase16(s.c_str());
	results.push_back(iterator);
	results.push_back(iterator);
	return results;

}

/*
std::vector<Int> KeyFinder::getIncrementorKeyModel(std::vector<Int> incrementors, std::vector<Int> keys ) {

	std::vector<Int> results;
	std::string sModel = model.GetBase16().c_str();
	int stepSize = 2;// 4;
	bool isFinished = false;

	int padding = 0;

	while (!isFinished) {
		std::string s;
		int pad = 0;
		while (pad < padding) {
			s += "0";
			pad++;
			if (s.size() >= sModel.size()) break;
		}

		s = "1" + s;
		if (!s.empty()) {
			Int iterator;
			iterator.SetBase16(s.c_str());
			results.push_back(iterator);
		}

		padding += stepSize;

		if (s.size() >= sModel.size()) isFinished = true;

	}

	results.insert(results.begin(), Int((uint64_t)0));
	for (int i = 0; i < 3; i++) {
		results.pop_back();
	}
	return results;

}
*/
std::vector<Int> KeyFinder::getGPUAssistedRandoms(Int min, Int max, uint64_t length) {

	//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
	//randomizers are a [col][row] vector of random values from 0 to 0xffff;
	//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
	//- so that randomizers doesn't necessarily have to match len() - handy for experimentation
	//Why the staticpool[randomizers] gimmick? Well, in the future I'm hoping to deprioritize some of the available range based on ML predictions.
	//Logger::log(LogLevel::Debug, "getGPUAssistedRandoms(), Range: " + min.toString() + ":" + max.toString());
	//Logger::log(LogLevel::Info, "GPU Assisted RNG: " + util::formatThousands(length) + " key parts.");

	//std::vector<std::vector<unsigned int>>().swap(mRandomizers);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	Random::GPURand rndGPU;

	//reusable randomizers.. disabled for now.
	rndGPU.loadRandomizers(timeSeed, length, max.DeriveRandomizerWidth());
	//mRandomizers = rndGPU.getRandomizers();

	return rndGPU.getRandomPool(min, max, length);
}


std::vector<Int> KeyFinder::getGPUAssistedRandoms32(Int min, Int max, uint64_t length) {

	//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
	//randomizers are a [col][row] vector of random values from 0 to 0xffff;
	//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
	//- so that randomizers doesn't necessarily have to match len() - handy for experimentation
	//Why the staticpool[randomizers] gimmick? Well, in the future I'm hoping to deprioritize some of the available range based on ML predictions.
	//Logger::log(LogLevel::Debug, "getGPUAssistedRandoms(), Range: " + min.toString() + ":" + max.toString());
	//Logger::log(LogLevel::Info, "GPU Assisted RNG: " + util::formatThousands(length) + " key parts.");

	//std::vector<std::vector<unsigned int>>().swap(mRandomizers);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	Random::GPURand rndGPU;

	//reusable randomizers.. disabled for now.
	rndGPU.load32BitBuffer(timeSeed, length);

	//mRandomizers = rndGPU.getRandomizers();

	return rndGPU.getRandomPool(min, max, length);
}
/*
fractal linear number scan?
bignet / small net number gen?
*/

bool custom_comparer(Int val1, Int val2) {
	return val1.IsLower(&val2);
}

std::vector<Int> KeyFinder::getGPURandomsEDX(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {
	//uses thrust, doesn't sort the net, uses max-min as spacing
	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	uint64_t NET_SIZE = 1024;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;

	//Int floor = Int((uint64_t)65535);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE);
	if (true) rndGPU.loadRandomizers(timeSeed, NET_SIZE, max.DeriveRandomizerWidth());

	uint64_t middleKeyCount = length / NET_SIZE;
	uint64_t spacerCount = length;

	vector<Int> results;
	vector<Int> preResults = rndGPU.getRandomPool(min, max, NET_SIZE);

	Int totalDistance = max;
	totalDistance.Sub(&min);

	Int spacing = totalDistance;
	spacing.Div(spacerCount);

	//sort doesn't matter in this method - std::sort(preResults.begin(), preResults.end(), custom_comparer);

	int lastIndex = preResults.size() - 1;
	for (int i = 0; i < preResults.size(); i++) {

		Int adjustedKey = preResults[i];
		adjustedKey.ZeroRight(right_pad);
		results.push_back(adjustedKey);

		Int previousKey = preResults[i];
		for (int m = 0; m < middleKeyCount; m++) {
			previousKey.Add(&spacing);

			if (previousKey.IsGreater(&max)) {
				previousKey = max;
				Int reverse_Spacing = spacing;
				reverse_Spacing.Div(2 * (m + 1));
				previousKey.Sub(&reverse_Spacing);
			}

			adjustedKey = previousKey;
			adjustedKey.ZeroRight(right_pad);
			results.push_back(adjustedKey);
		}

	}


	return results;
}

//uses thrust, doesn't sort the net, uses max-min as spacing
std::vector<Int> KeyFinder::getGPURandomsED(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {

	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	uint64_t NET_SIZE = 1024;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;

	//Int floor = Int((uint64_t)65535);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE);
	if (true) rndGPU.loadRandomizers(timeSeed, NET_SIZE, max.DeriveRandomizerWidth());

	uint64_t middleKeyCount = length / NET_SIZE;
	uint64_t spacerCount = length;

	vector<Int> results;
	vector<Int> preResults = rndGPU.getRandomPool(min, max, NET_SIZE);

	Int totalDistance = max;
	totalDistance.Sub(&min);

	Int spacing = totalDistance;
	spacing.Div(spacerCount);

	//sort doesn't matter in this method - std::sort(preResults.begin(), preResults.end(), custom_comparer);

	int lastIndex = preResults.size() - 1;
	for (int i = 0; i < preResults.size(); i++) {

		Int adjustedKey = preResults[i];
		adjustedKey.ZeroRight(right_pad);
		results.push_back(adjustedKey);

		Int previousKey = preResults[i];
		for (int m = 0; m < middleKeyCount; m++) {
			previousKey.Add(&spacing); 

			if (previousKey.IsGreater(&max)) {
				previousKey = max;
				Int reverse_Spacing = spacing;
				reverse_Spacing.Div(2 * (m+1));
				previousKey.Sub(&reverse_Spacing);
			}

			adjustedKey = previousKey;
			adjustedKey.ZeroRight(right_pad);
			results.push_back(adjustedKey);
		}

	}


	return results;
}

//uses thrust, sorts the net, uses middle distance division as spacing
std::vector<Int> KeyFinder::getGPURandomsED2(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {

	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	uint64_t NET_SIZE = 1024;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;

	//Int floor = Int((uint64_t)65535);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE);
	if (true) rndGPU.loadRandomizers(timeSeed, NET_SIZE, max.DeriveRandomizerWidth());

	uint64_t middleKeyCount = length / NET_SIZE;

	vector<Int> results;
	vector<Int> preResults = rndGPU.getRandomPool(min, max, NET_SIZE);


	std::sort(preResults.begin(), preResults.end(), custom_comparer);

	int lastIndex = preResults.size() - 1;
	for (int i = 0; i < preResults.size(); i++) {
		Int distance;
		if (i == 0) {
			distance = preResults[i];
			distance.Sub(&min);
		}
		else if (i == lastIndex) {
			distance = max;
			distance.Sub(&preResults[lastIndex]);
		}
		else {
			distance = preResults[i + 1];
			distance.Sub(&preResults[i]);
		}

		Int spacing = distance;

		spacing.Div(middleKeyCount);

		Int adjustedKey = preResults[i];
		adjustedKey.ZeroRight(right_pad);
		results.push_back(adjustedKey);

		Int previousKey = preResults[i];
		for (int m = 0; m < middleKeyCount; m++) {
			previousKey.Add(&spacing);  //binary feather any overflow back into the range
			if (previousKey.IsGreater(&max)) {
				previousKey = max;
				Int reverse_Spacing = spacing;
				reverse_Spacing.Div(2 * (m + 1));
				previousKey.Sub(&reverse_Spacing);
			}
			adjustedKey = previousKey;
			adjustedKey.ZeroRight(right_pad);
			results.push_back(adjustedKey);
		}

	}


	return results;
}

//chunksort based distance calc
std::vector<Int> KeyFinder::getGPURandomsED3(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {
	//uses thrust, sorts the net, uses chunked(len) avg as spacing

	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	int CHUNK_SIZE = 4;
	int KEY_LEN = max.GetBase16().size();

	uint64_t NET_SIZE = 8192;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;

	//Int floor = Int((uint64_t)65535);
	uint32_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE);
	if (true) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE, max.DeriveRandomizerWidth());

	uint64_t middleKeyCount = length / NET_SIZE;

	vector<Int> results;
	vector<Int> preResults = rndGPU.getRandomPool(min, max, NET_SIZE);

	//std::sort(preResults.begin(), preResults.end(), custom_comparer);

	Int MAX_CHUNK_VAL;
	MAX_CHUNK_VAL.DeriveEndKey(CHUNK_SIZE);


	Int chunkDistance;
	chunkDistance = chunkDistance.GetChunkDistance(CHUNK_SIZE);

	int lastIndex = preResults.size() - 1;
	for (int i = 0; i < preResults.size(); i++) {
		Int thisKey = preResults[i];
		std::vector<Int> chunks = thisKey.GetChunks(CHUNK_SIZE);

		/*
		for (int x = 0; x < chunks.size(); x++) {
			OutputDebugString(chunks[x].GetBase16().c_str());
			OutputDebugString("\n");
		}
		OutputDebugString("\n");
		OutputDebugString("\n");*/

		Int keyPartDistance = chunkDistance;
		keyPartDistance.Div(middleKeyCount);

		Int adjustedKey = preResults[i];
		//adjustedKey.ZeroRight(right_pad);
		results.push_back(adjustedKey);

		Int previousKey = preResults[i];
		for (int m = 0; m < middleKeyCount - 1; m++) {

			for (int j = 1; j < chunks.size() -1; j++) { //intentionally leaving the first and last chunk alone.   first chunk is for targeting range and last chunk will be walked by gpu if default stride
				chunks[j].Add(&keyPartDistance);
			}

			Int derivedKey;
			derivedKey.SetInt32(0);
			derivedKey.SetChunks(chunks, KEY_LEN);
			derivedKey.ZeroRight(right_pad);

			results.push_back(derivedKey);
		}

	}


	return results;
}

//chunksort based middle-distance division calc
std::vector<Int> KeyFinder::getGPURandomsED4(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {
	//uses thrust, sorts the net, uses chunked middle distance division as spacing

	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	int KEY_LEN = max.GetBase16().size();
	int CHUNK_SIZE = 4;
	uint64_t NET_SIZE = 1024;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;

	//Int floor = Int((uint64_t)65535);
	//uint16_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	uint32_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	//if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE);
	if (true) 	rndGPU.loadRandomizers(timeSeed, NET_SIZE, max.DeriveRandomizerWidth());

	uint64_t middleKeyCount = length / NET_SIZE;

	vector<Int> results;
	vector<Int> preResults = rndGPU.getRandomPool(min, max, NET_SIZE);

	//vector<Int> chunks = preResults[0].GetChunks(4);
	//Int fullCircle;
	//fullCircle.SetChunks(chunks);

	std::sort(preResults.begin(), preResults.end(), custom_comparer);

	int lastIndex = preResults.size() - 1;
	for (int i = 0; i < preResults.size(); i++) {
		Int thisKey = preResults[i];
		std::vector<Int> chunks = thisKey.GetChunks(CHUNK_SIZE);

		Int compareKey;
		if (i == 0) {
			compareKey = &min;
		}
		else if (i == lastIndex) {
			compareKey = max;
		}
		else {
			compareKey = preResults[i + 1];
		}

		std::vector<Int> compareChunks = compareKey.GetChunks(CHUNK_SIZE);
		std::vector<Int> keyPartDistances;	
		//Int firstDistance;
		//firstDistance.SetInt32(0);;

		//keyPartDistances.push_back(firstDistance);

		for (int j = 0; j < chunks.size(); j++) {
			Int keyPartA = chunks[j];
			Int keyPartB = compareChunks[j];

			Int keyPartDistance;
			keyPartDistance.SetInt32(0);
			if (keyPartA.IsGreater(&keyPartB)) {
				keyPartDistance = keyPartA;
				keyPartDistance.Sub(&keyPartB);
			}
			else {
				keyPartDistance = keyPartB;
				keyPartDistance.Sub(&keyPartA);
			}
			keyPartDistance.Div(middleKeyCount);
			keyPartDistances.push_back(keyPartDistance);
		}

		Int adjustedKey = preResults[i];
		adjustedKey.ZeroRight(right_pad);
		results.push_back(adjustedKey);

		Int previousKey = preResults[i];
		for (int m = 0; m < middleKeyCount - 1; m++) {
			
			for (int j = 1; j < chunks.size()-1; j++) {  //intentionally skip the first and last chunk, first is targeting the range and last will be walked by the gpu 
				Int spacing = keyPartDistances[j];
				chunks[j].Add(&keyPartDistances[j]);
			}

			Int derivedKey;
			derivedKey.SetInt32(0);
			derivedKey.SetChunks(chunks, KEY_LEN);
			derivedKey.ZeroRight(right_pad);

			results.push_back(derivedKey);
		}

	}
	

	return results;
}

//uses thrust, sorts the net, uses middle distance division as spacing
std::vector<Int> KeyFinder::getGPURandoms_Masked_Oversample(Random::GPURand& rndGPU, Int min, Int max, uint64_t length, uint64_t rKeyPer) {

	Int rKeyMask;
	rKeyMask.SetInt64(rKeyPer);

	
	uint64_t SAMPLE_POOL_SIZE = 1024 * 32768;

	Int POOL_MIN;
	Int POOL_MAX;

	POOL_MIN.SetBase16("1000000000000000000000000000000000000000000000000000000000000000");
	POOL_MAX.SetBase16("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");


	uint64_t NET_SIZE = 3200;
	uint64_t MASK_SIZE = POOL_MAX.GetBase16Length() -  max.GetBase16Length();

	NET_SIZE = length;
	uint32_t right_pad = rKeyMask.GetBase16Length() - 1;
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	rndGPU.loadRandomizers(timeSeed, SAMPLE_POOL_SIZE, POOL_MAX.DeriveRandomizerWidth());


	vector<Int> results;
	vector<Int> preResults;
	vector<Int> keyPool = rndGPU.getRandomPool(POOL_MIN, POOL_MAX, SAMPLE_POOL_SIZE);
	if (rKeyCount2 == 0) {
		if (display > 0) {
			printf("  Generator : Generated %d full-length starting keys\n", keyPool.size());
		}
	}

	for (int k = 0; k < keyPool.size(); k++) {
		Int maskedKey = keyPool[k];
		maskedKey.ZeroLeft(MASK_SIZE);

		if (maskedKey.IsGreater(&min) && maskedKey.IsLower(&max)) {
			preResults.push_back(maskedKey);
		}
		if (preResults.size() == NET_SIZE) break;
	}


	uint64_t middleKeyCount = length / preResults.size();
	int overFlowCount = 0;
	int middleCount = 0;
	int lastIndex = preResults.size() - 1;

	if (preResults.size() < length) { //only sort and middle key if there are keys remaining to generate
		if (rKeyCount2 == 0) {
			if (display > 0) {
				printf("  Generator : Extracted %d masked keys for given keyspace range\n", preResults.size());
			}
		}

		std::sort(preResults.begin(), preResults.end(), custom_comparer);

		if (rKeyCount2 == 0) {
			if (display > 0) {
				printf("  Generator : Sorted %d base keys\n", preResults.size());
				printf("  Generator : Interleaving %d middle keys per base key\n", middleKeyCount);
			}
		}

		for (int i = 0; i < preResults.size(); i++) {
			Int distance;
			if (i == 0) {
				distance = preResults[i];
				distance.Sub(&min);
			}
			else if (i == lastIndex) {
				distance = max;
				distance.Sub(&preResults[lastIndex]);
			}
			else {
				distance = preResults[i + 1];
				distance.Sub(&preResults[i]);
			}

			Int spacing = distance;

			spacing.Div(middleKeyCount);

			Int adjustedKey = preResults[i];
			adjustedKey.ZeroRight(right_pad);
			results.push_back(adjustedKey);

			Int previousKey = preResults[i];
			for (int m = 0; m < middleKeyCount; m++) {
				previousKey.Add(&spacing);  //binary feather any overflow back into the range
				if (previousKey.IsGreater(&max)) {
					previousKey = max;
					Int reverse_Spacing = spacing;
					reverse_Spacing.Div(2 * (m + 1));
					previousKey.Sub(&reverse_Spacing);
					overFlowCount++;
				}
				adjustedKey = previousKey;
				adjustedKey.ZeroRight(right_pad);
				results.push_back(adjustedKey);
				middleCount++;
			}

		}
	}
	else {
		results = preResults;
	}

	if (rKeyCount2 == 0) {
		if (display > 0) {
			printf("  Generator : %d middle keys generated\n", middleCount);
			if (overFlowCount > 0) {
				printf("  Generator : %d overflow keys remediated\n", overFlowCount);
			}
			printf("  Generator : %d starting keys generated.\n\n", (int)results.size());
		}
	}

	return results;
}


std::vector<Int> KeyFinder::getGPUAssistedMaskedRandoms(Random::GPURand& rndGPU, Int min, Int max, uint64_t length) {

	//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
	//randomizers are a [col][row] vector of random values from 0 to 0xffff;
	//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
	//- so that randomizers doesn't necessarily have to match len() - handy for experimentation
	//Why the staticpool[randomizers] gimmick? Well, in the future I'm hoping to deprioritize some of the available range based on ML predictions.
	//Logger::log(LogLevel::Debug, "getGPUAssistedRandoms(), Range: " + min.toString() + ":" + max.toString());
	//Logger::log(LogLevel::Info, "GPU Assisted RNG: " + util::formatThousands(length) + " key parts.");

	uint64_t MAX_ITERATIONS_PER = 1;//16;//256;//  16;// 64;// 16;//256;
	Int floor = Int((uint64_t)65535);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	if (rndGPU.getStaticPrefixBufferSize()==0) rndGPU.loadStaticPrefixes(&min, &max, 3);
	if (rndGPU.getRandomizersSize() == 0) 	rndGPU.loadRandomizers(timeSeed, length, max.DeriveRandomizerWidth());
	if (rndGPU.getIncrementorsSize() == 0) 	rndGPU.setIncrementors(getRandomIncrementors(min));

	Int pfx;
	Int incrementor = rndGPU.getCurrentIncrementor();
	
	
	if (incrementor_iterations >= MAX_ITERATIONS_PER || (incrementor.IsLowerOrEqual(&floor) && incrementor_iterations>1)) {
		incrementor_iterations = 0;
		incrementor = rndGPU.getNextIncrementor(false);
	}
	else {
		incrementor = rndGPU.getCurrentIncrementor();
		incrementor_iterations++;
	}
	
	if (!incrementor.IsZero()) {
		pfx = rndGPU.getCurrentPrefix();
	}
	else {
		rndGPU.resetIncrementorIndex();
		pfx = rndGPU.getNextPrefix(false);
	}
	
	/*
	if (incrementor_iterations == 0) {
		pfx = rndGPU.getCurrentPrefix();
	}
	else {
		pfx = rndGPU.getNextPrefix(false);
	}
	*/

	incrementor_iterations++;
	if (display > 0) {
		printf("\r	      										prefix: [%s] [%d/%d] pass: [%d]	incrementor: [%s]	iteration: [%d]                    ", pfx.GetBase16().c_str(), rndGPU.getPrefixIndex() +1, rndGPU.getPrefixCount(), prefix_cycles + 1, incrementor.GetBase16().c_str(), incrementor_iterations);
	}

	if (incrementor.IsZero() && pfx.IsZero()) {
		prefix_cycles++;
		rndGPU.loadRandomizers(timeSeed, length, max.DeriveRandomizerWidth());
		rndGPU.resetPrefixIndex();
		rndGPU.resetIncrementorIndex();
	}

	vector<Int> results = rndGPU.getMaskedRandomPool(pfx, min, max, length);

	for (int i = 0; i < length; i++) {
		for (int j = 0; j < incrementor_iterations; j++) {
			results[i].Add(&incrementor);
		}
	}

	
	return results;
}


std::vector<Int> KeyFinder::getGPUAssistedMaskedRandoms(Int min, Int max, uint64_t length) {

	//bufferpool is a sequential vector of unsigned ints from 0 to 0xffff; 
	//randomizers are a [col][row] vector of random values from 0 to 0xffff;
	//Constructs a random uint256 by assembling uint.v[column] from bufferpool[randomizers[col][row]
	//- so that randomizers doesn't necessarily have to match len() - handy for experimentation
	//Why the staticpool[randomizers] gimmick? Well, in the future I'm hoping to deprioritize some of the available range based on ML predictions.
	//Logger::log(LogLevel::Debug, "getGPUAssistedRandoms(), Range: " + min.toString() + ":" + max.toString());
	//Logger::log(LogLevel::Info, "GPU Assisted RNG: " + util::formatThousands(length) + " key parts.");

	//std::vector<std::vector<unsigned int>>().swap(mRandomizers);
	uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	Random::GPURand rndGPU;

	//reusable randomizers.. disabled for now.
	rndGPU.loadRandomizers(timeSeed, length, max.DeriveRandomizerWidth());
	//mRandomizers = rndGPU.getRandomizers();

	rndGPU.loadStaticPrefixes(min, max, 2);

	return rndGPU.getMaskedRandomPool(min, max, length);
}

void KeyFinder::getGPUStartingKeys(Int & tRangeStart, Int & tRangeEnd, int groupSize, int nbThread, Int * keys, Point * p)
{
	int REPORTING_SIZE = 256;
	if (rKey > 0) {
		
		if (rangeDiff2.GetBitLength() > 1) {
			
			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}
			
			for (int i = 0; i < nbThread; i++) {
				
				gpucores = i;
				keys[i].Rand2(&rangeStart8, &rangeEnd);;
				rhex = keys[i];
				
				if (display > 0 && (i % REPORTING_SIZE == 0)) {
					printf("\r  [Thread: %d]   [%s] ", i, keys[i].GetBase16().c_str());
				}
				
				Int k(keys + i);
				k.Add((uint64_t)(groupSize / 2));
				p[i] = secp->ComputePublicKey(&k);
			}
		}
		else {
			if (next > 0) {

				if (rKeyCount2 == 0) {

					if (next > 256) {
						printf("\n  ROTOR Random : Are you serious %d bit ??? \n", next);
						exit(1);
					}
					if (zet > 256) {
						printf("\n  ROTOR Random : Are you serious -z %d bit ??? \n", zet);
						exit(1);
					}
					if (zet < 1) {

						if (display > 0) {
							printf("  ROTOR Random : Private keys random %d (bit)  \n", next);
						}
					}
					else {

						if (display > 0) {
							printf("  ROTOR Random : Private keys random %d (bit) <~> %d (bit)\n", next, zet);
						}
					}
					if (display > 0) {
						printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n\n", nbThread, rKey);
					}
				}

				for (int i = 0; i < nbThread; i++) {
					
					gpucores = i;
					int next2 = 0;

					if (zet < 1) {

						keys[i].Rand(next);
						rhex = keys;
						if (display > 0) {
							printf("\r  [Thread: %d]   [%s] ", i, keys[i].GetBase16().c_str());
						}
						Int k(keys + i);
						k.Add((uint64_t)(groupSize / 2));
						p[i] = secp->ComputePublicKey(&k);
					}
					else {
						if (zet <= next) {
							printf("\n  ROTOR Random : Are you serious -n %d (start) -z %d (end) ??? \n  The start must be less than the end \n", next, zet);
							exit(1);
						}
						int dfs = zet - next;
						srand(time(NULL));
						int next3 = next + rand() % dfs;
						next2 = next3 + rand() % 2;
						keys[i].Rand(next2);
						rhex = keys;
						if (display > 0) {
							printf("\r  [Thread: %d]   [%s] ", i, keys[i].GetBase16().c_str());
						}
						Int k(keys + i);
						k.Add((uint64_t)(groupSize / 2));
						p[i] = secp->ComputePublicKey(&k);
						
					}
				}
			}
			else {
				if (rKeyCount2 == 0) {
					if (display > 0) {
						printf("  Rotor Random : Private keys random 95%% (252-256) bit + 5%% (248-252) bit\n");
						printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n\n", nbThread, rKey);
					}
				}

				for (int i = 0; i < nbThread; i++) {
					
					gpucores = i;
					keys[i].Rand(256);
					rhex = keys;
					if (display > 0) {
						
						printf("\r  [Thread: %d]   [%s] ", i, keys[i].GetBase16().c_str());
					}

					Int k(keys + i);
					k.Add((uint64_t)(groupSize / 2));
					p[i] = secp->ComputePublicKey(&k);
				}
			}
		}
	}
	else {
		
		Int tThreads;
		tThreads.SetInt32(nbThread);
		Int tRangeDiff(tRangeEnd);
		Int tRangeStart2(tRangeStart);
		Int tRangeEnd2(tRangeStart);

		tRangeDiff.Set(&tRangeEnd);
		tRangeDiff.Sub(&tRangeStart);
		razn = tRangeDiff;

		tRangeDiff.Div(&tThreads);
		
		int rangeShowThreasold = 3;
		int rangeShowCounter = 0;
		uint64_t nextt = 0;
		if (value777 > 1) {
			nextt = value777 / nbThread;
			tRangeStart2.Add(nextt);
		}
		if (next > 0) { 

			if (display > 0) {
				printf("  info   : Save checkpoints every %d minutes. For continue range, run the bat file Rotor-Cuda_Continue.bat \n", next);
			}
		}
		gir.Set(&rangeDiff2);
		Int reh;
		uint64_t nextt99;
		nextt99 = value777 * 1;
		reh.Add(nextt99);
		gir.Sub(&reh);

		if (display > 0) {
			if (value777 > 1) {
				printf("\n  info   : Continuation... Divide the remaining range %s (%d bit) into GPU %d threads \n\n", gir.GetBase16().c_str(), gir.GetBitLength(), nbThread);
			}
			else {
				printf("\n  info   : Divide the range %s (%d bit) into GPU %d threads \n\n", rangeDiff2.GetBase16().c_str(), gir.GetBitLength(), nbThread);
			}
		}
		
		for (int i = 0; i < nbThread + 1; i++) {
			gpucores = i;
			tRangeEnd2.Set(&tRangeStart2);
			tRangeEnd2.Add(&tRangeDiff);

			keys[i].Set(&tRangeStart2);
			if (i == 0) {
				if (display > 0) {
					printf("  Thread 00000 : %s ->", keys[i].GetBase16().c_str());
				}
			}
			Int dobb;
			dobb.Set(&tRangeStart2);
			dobb.Add(&tRangeDiff);
			dobb.Sub(nextt);
			if (display > 0) {

				if (i == 0) {
					printf(" %s \n", dobb.GetBase16().c_str());
				}
				if (i == 1) {
					printf("  Thread 00001 : %s -> %s \n", tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
				}
				if (i == 2) {
					printf("  Thread 00002 : %s -> %s \n", tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
				}
				if (i == 3) {
					printf("  Thread 00003 : %s -> %s \n", tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
					printf("           ... : \n");
				}
				if (i == nbThread - 2) {
					printf("  Thread %d : %s -> %s \n", i, tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
				}
				if (i == nbThread - 1) {
					printf("  Thread %d : %s -> %s \n", i, tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
				}
				if (i == nbThread) {
					printf("  Thread %d : %s -> %s \n\n", i, tRangeStart2.GetBase16().c_str(), dobb.GetBase16().c_str());
				}
			}
			tRangeStart2.Add(&tRangeDiff);
			Int k(keys + i);
			k.Add((uint64_t)(groupSize / 2));
			p[i] = secp->ComputePublicKey(&k);
		}
	}
}

void KeyFinder::getGPUStartingKeys2(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			Int* keyParts = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {
				gpucores = keyCount;
				
				//base random
				Int kRnd;
				kRnd.Rand3(&rangeStart8, &rangeEnd);
				
				//std::string kRndBase = kRnd.GetBase16();   //for inspection

				kRnd.GetChunks(1);

				//derive from base random
				std::vector<Int> derivations = kRnd.Derive(&kRnd);

				//iterate through derived keys and make new ones through 32bit shifts
				for (int i = 0; i < derivations.size(); i++) {
					if (keyCount < nbThread) {
						keys[keyCount] = derivations[i];
						rhex = keys[keyCount];

						//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

						if (display > 0 && (keyCount % 1024 == 0)) {
							printf("\r  [Thread: %d]   [%s] ", i, keys[keyCount].GetBase16().c_str());
						}

						Int k(keys + keyCount);
						k.Add((uint64_t)(groupSize / 2));
						p[keyCount] = secp->ComputePublicKey(&k);

						keyCount++;
					}
					else {
						generatorContinue = false;
						break;
					}
				}
			}
		}

	}
}

void KeyFinder::getGPUStartingKeys3(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			Int* keyParts = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {
				gpucores = keyCount;

				//base random
				Int kRnd;
				kRnd.Rand3(&rangeStart8, &rangeEnd);

				//std::string kRndBase = kRnd.GetBase16();   //for inspection

				//derive from base random
				std::vector<Int> derivations = kRnd.Derive(&kRnd);

				//iterate through derived keys and make new ones through 32bit shifts
				for (int i = 0; i < derivations.size(); i++) {
					if (keyCount < nbThread) {
						keys[keyCount] = derivations[i];
						rhex = keys[keyCount];

						//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

						if (display > 0 && (keyCount % 1024 == 0)) {
							printf("\r  [Thread: %d]   [%s] ", i, keys[keyCount].GetBase16().c_str());
						}

						Int k(keys + keyCount);
						k.Add((uint64_t)(groupSize / 2));
						p[keyCount] = secp->ComputePublicKey(&k);

						keyCount++;
					}
					else {
						generatorContinue = false;
						break;
					}
				}
			}
		}

	}
}

void KeyFinder::getEvenlyDistributedGPUStartingKeys(Random::GPURand& rndGPU, Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	int REPORTING_INTERVAL = 128;//256;//2048; //256;// 8192;
	if (rKey > 0) {
		//printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
		//printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  Random : Max %d (bit) %s \n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
					printf("  Method : Evenly Distributed, Thrust RNG, 16bit Assembly\n\n");
				}
			}

			//Determine the right pad space by rKey 
			uint64_t rKeyMultiplier = 1000000000;
			uint64_t rKeyEst = (rKeyMultiplier * rKey);
			uint64_t rKeyPer = rKeyEst / nbThread;

			std::vector<Int> initial_Randoms = getGPURandomsED3(rndGPU, rangeStart, rangeEnd, nbThread, rKeyPer);
			//std::vector<Int> initial_Randoms = getGPURandomsED4(rndGPU, rangeStart, rangeEnd, nbThread, rKeyPer);

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {


				if (keyCount < nbThread) {

					gpucores = keyCount;

					keys[keyCount] = initial_Randoms[keyCount];
					//keys[keyCount] = kRnd;
					rhex = keys[keyCount];

					//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

					if (display > 0 && (keyCount % 8192 == 0)) {
						printf("\r  [Thread: %d]   [%s] ", keyCount, keys[keyCount].GetBase16().c_str());
					}

					Int k(keys + keyCount);
					k.Add((uint64_t)(groupSize / 2));
					p[keyCount] = secp->ComputePublicKey(&k);

					keyCount++;
				}
				else {
					generatorContinue = false;
					break;
				}


			}
		}

	}
}

void KeyFinder::getOverSampledGPUStartingKeys(Random::GPURand& rndGPU, Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	int REPORTING_INTERVAL = 128;
	if (rKey > 0) {
		//printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
		//printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  Random : Max %d (bit) %s \n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
					printf("  Method : Oversampled, then masked to keyspace, Thrust RNG, 16bit Assembly\n");
				}
			}

			//Determine the right pad space by rKey 
			uint64_t rKeyMultiplier = 1000000000;
			uint64_t rKeyEst = (rKeyMultiplier * rKey);
			uint64_t rKeyPer = rKeyEst / nbThread;

			std::vector<Int> initial_Randoms = getGPURandoms_Masked_Oversample(rndGPU, rangeStart, rangeEnd, nbThread, rKeyPer);

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {


				if (keyCount < nbThread) {

					gpucores = keyCount;

					keys[keyCount] = initial_Randoms[keyCount];
					//keys[keyCount] = kRnd;
					rhex = keys[keyCount];

					//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

					if (display > 0 && (keyCount % 8192 == 0)) {
						printf("\r  [Thread: %d]   [%s] ", keyCount, keys[keyCount].GetBase16().c_str());
					}

					Int k(keys + keyCount);
					k.Add((uint64_t)(groupSize / 2));
					p[keyCount] = secp->ComputePublicKey(&k);

					keyCount++;
				}
				else {
					generatorContinue = false;
					break;
				}


			}
		}

	}
}


void KeyFinder::getGPUStartingKeysViaThrust(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	int REPORTING_INTERVAL = 2048; //256;// 8192;
	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			std::vector<Int> initial_Randoms = getGPUAssistedMaskedRandoms(rangeStart, rangeEnd, nbThread);
			//std::vector<Int> initial_Randoms = getGPUAssistedRandoms(rangeStart, rangeEnd, nbThread);

			Int* keyParts = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {


				if (keyCount < nbThread) {

					gpucores = keyCount;

					//base random
					Int kRnd = initial_Randoms[keyCount];

					keys[keyCount] = kRnd;
					rhex = keys[keyCount];

					//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

					if (display > 0 && (keyCount % 8192 == 0)) {
						printf("\r  [Thread: %d]   [%s] ", keyCount, keys[keyCount].GetBase16().c_str());
					}

					Int k(keys + keyCount);
					k.Add((uint64_t)(groupSize / 2));
					p[keyCount] = secp->ComputePublicKey(&k);

					keyCount++;
				}
				else {
					generatorContinue = false;
					break;
				}


			}
		}

	}
}

void KeyFinder::getGPUStartingKeysViaThrust(Random::GPURand& rndGPU, Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, Point* p)
{
	int REPORTING_INTERVAL = 2048; //256;// 8192;
	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			std::vector<Int> initial_Randoms = getGPUAssistedMaskedRandoms(rndGPU, rangeStart, rangeEnd, nbThread);
			//std::vector<Int> initial_Randoms = getGPUAssistedRandoms(rangeStart, rangeEnd, nbThread);

			//Int* keyParts = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {


				if (keyCount < nbThread) {

					gpucores = keyCount;

					//base random
					//Int kRnd = initial_Randoms[keyCount];

					//kRnd.GetChunks();

					keys[keyCount] = initial_Randoms[keyCount];
					//keys[keyCount] = kRnd;
					rhex = keys[keyCount];

					//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

					if (display > 0 && (keyCount % 8192 == 0)) {
						printf("\r  [Thread: %d]   [%s] ", keyCount, keys[keyCount].GetBase16().c_str());
					}

					Int k(keys + keyCount);
					k.Add((uint64_t)(groupSize / 2));
					p[keyCount] = secp->ComputePublicKey(&k);

					keyCount++;
				}
				else {
					generatorContinue = false;
					break;
				}


			}
		}

	}
}

void KeyFinder::getGPUStartingKeysAndParts(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, std::vector<std::vector<Int>>* keyParts,  Point* p)
{
	Int zero;
	zero.SetInt32(0);
	std::vector<std::vector<Int>>().swap(*keyParts);

	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			Int* keys = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {
				gpucores = keyCount;

				//base random
				Int kRnd;
				kRnd.Rand3(&rangeStart8, &rangeEnd);

				//std::string kRndBase = kRnd.GetBase16();   //for inspection


				//rnd fill rate opportunity here
				if (keyCount < nbThread) {
					keys[keyCount] = kRnd;
					rhex = keys[keyCount];

					if (display > 0 && (keyCount % 1024 == 0)) {
						printf("\r  [Thread: %d]   [%s] ", keyCount, keys[keyCount].GetBase16().c_str());
					}

					Int k(keys + keyCount);
					k.Add((uint64_t)(groupSize / 2));
					p[keyCount] = secp->ComputePublicKey(&k);


					//break out this starting key into [hhhh] chunks 

					std::vector<Int> singularParts = kRnd.GetChunks(1);
					reverse(singularParts.begin(), singularParts.end());

					std::vector<Int> groupedParts;
					//std::vector<string> groupedPartsStr;

					int PART_SIZE = 4;
					int psz = 1;
					//Int kp = singularParts[singularParts.size()-1-PART_SIZE];

					Int kp;
					kp.SetInt32(0);

					int parseIndex = singularParts.size() - (PART_SIZE);
					bool partParsed = false;
					while (!partParsed) {
						if (parseIndex > 0) {
							for (int i = 0; i < PART_SIZE; i++) {
								kp.Mult(16);	//expand part
								kp.Add(&singularParts[parseIndex + i]);   //add to expanded portion
							}
							groupedParts.push_back(kp);
							//groupedPartsStr.push_back(kp.GetBase16());
							kp.SetInt32(0);
						}
						else {
							int TRUNCATED_PART_SIZE = PART_SIZE + parseIndex;  //parseindex is {negative~zero} in this case
							for (int i = 0;i < TRUNCATED_PART_SIZE; i++) {
								kp.Mult(16);	//expand part
								kp.Add(&singularParts[i]);		//add to expanded portion
							}
							groupedParts.push_back(kp);
							//groupedPartsStr.push_back(kp.GetBase16());
							kp.SetInt32(0);
							partParsed = true;
						}
						parseIndex = parseIndex - PART_SIZE;
					}

					reverse(groupedParts.begin(), groupedParts.end());
					//reverse(groupedPartsStr.begin(), groupedPartsStr.end());

					keyParts->push_back(groupedParts);


					keyCount++;
				}
				else {
					generatorContinue = false;
					break;
				}

			}
		}

	}
}

void KeyFinder::getGPUStartingKeysWithParts(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Int* keys, std::vector<std::vector<Int>>* keyParts, Point* p)
{
	if (rKey > 0) {

		if (rangeDiff2.GetBitLength() > 1) {

			if (rKeyCount2 == 0) {
				if (display > 0) {
					printf("  Start Keys : Randomly regenerates %d starting keys every %llu,000,000,000 on the counter\n", nbThread, rKey);
					printf("  ROTOR Random : Min %d (bit) %s \n", rangeStart.GetBitLength(), rangeStart.GetBase16().c_str());
					printf("  ROTOR Random : Max %d (bit) %s \n\n", rangeEnd.GetBitLength(), rangeEnd.GetBase16().c_str());
				}
			}

			Int* keyParts = new Int[nbThread];

			bool generatorContinue = true;
			int keyCount = 0;
			while (generatorContinue) {
				gpucores = keyCount;

				//base random
				Int kRnd;
				kRnd.Rand3(&rangeStart8, &rangeEnd);

				//std::string kRndBase = kRnd.GetBase16();   //for inspection

				kRnd.GetChunks(1);

				//derive from base random
				std::vector<Int> derivations = kRnd.Derive(&kRnd);

				//iterate through derived keys and make new ones through 32bit shifts
				for (int i = 0; i < derivations.size(); i++) {
					if (keyCount < nbThread) {
						keys[keyCount] = derivations[i];
						rhex = keys[keyCount];

						//std::string derivedKey = keys[keyCount].GetBase16();  //for inspection

						if (display > 0 && (keyCount % 1024 == 0)) {
							printf("\r  [Thread: %d]   [%s] ", i, keys[keyCount].GetBase16().c_str());
						}

						Int k(keys + keyCount);
						k.Add((uint64_t)(groupSize / 2));
						p[keyCount] = secp->ComputePublicKey(&k);

						keyCount++;
					}
					else {
						generatorContinue = false;
						break;
					}
				}
			}
		}

	}
}



void KeyFinder::FindKeyGPU(TH_PARAM* ph)
{

	bool ok = true;

#ifdef WITHGPU

	// Global init
	int thId = ph->threadId;
	Int tRangeStart = ph->rangeStart;
	Int tRangeEnd = ph->rangeEnd;

	GPUEngine* g;
	switch (searchMode) {
	case (int)SEARCH_MODE_MA:
	case (int)SEARCH_MODE_MX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			BLOOM_N, bloom->get_bits(), bloom->get_hashes(), bloom->get_bf(), DATA, TOTAL_COUNT, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SA:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			hash160Keccak, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			xpoint, (rKey != 0));
		break;
	default:
		printf("  Invalid search mode format!");
		return;
		break;
	}


	int nbThread = g->GetNbThread();
	Point* p = new Point[nbThread];
	Int* keys = new Int[nbThread];

	std::vector<ITEM> found;

	printf("  GPU          : %s\n", g->deviceName.c_str());

	counters[thId] = 0;

	getGPUStartingKeys2(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, p);
	ok = g->SetKeys(p);

	ph->hasStarted = true;
	ph->rKeyRequest = false;

	// GPU Thread
	while (ok && !endOfSearch) {

		if (ph->rKeyRequest) {
			getGPUStartingKeys2(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, p);
			ok = g->SetKeys(p);
			ph->rKeyRequest = false;
		}

		// Call kernel
		switch (searchMode) {
		case (int)SEARCH_MODE_MA:
			ok = g->LaunchSEARCH_MODE_MA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_MX:
			ok = g->LaunchSEARCH_MODE_MX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		case (int)SEARCH_MODE_SA:
			ok = g->LaunchSEARCH_MODE_SA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_SX:
			ok = g->LaunchSEARCH_MODE_SX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		default:
			break;
		}


		if (ok) {
			for (int i = 0; i < nbThread; i++) {
				keys[i].Add((uint64_t)STEP_SIZE);
			}
			counters[thId] += (uint64_t)(STEP_SIZE)*nbThread; // Point
		}

	}

	delete[] keys;
	delete[] p;
	delete g;

#else
	ph->hasStarted = true;
	printf("  GPU code not compiled, use -DWITHGPU when compiling.\n");
#endif

	ph->isRunning = false;

}

void KeyFinder::FindKeyGPUThrust(TH_PARAM* ph)
{

	bool ok = true;

#ifdef WITHGPU

	// Global init
	int thId = ph->threadId;
	Int tRangeStart = ph->rangeStart;
	Int tRangeEnd = ph->rangeEnd;

	GPUEngine* g;
	switch (searchMode) {
	case (int)SEARCH_MODE_MA:
	case (int)SEARCH_MODE_MX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			BLOOM_N, bloom->get_bits(), bloom->get_hashes(), bloom->get_bf(), DATA, TOTAL_COUNT, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SA:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			hash160Keccak, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			xpoint, (rKey != 0));
		break;
	default:
		printf("  Invalid search mode format!");
		return;
		break;
	}


	int nbThread = g->GetNbThread();
	Point* p = new Point[nbThread];
	Int* keys = new Int[nbThread];
	std::vector<ITEM> found;

	printf("  GPU          : %s\n", g->deviceName.c_str());

	counters[thId] = 0;
	int groupSize = g->GetGroupSize();

	Random::GPURand rndGPU;
	std::vector<Int> incrementors;

	//reusable static prefixes
	//rndGPU.loadStaticPrefixes(tRangeStart, tRangeEnd, 2);

	//getGPUStartingKeysAndParts(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, keyparts, p);
	//getGPUStartingKeysViaThrust(rndGPU, tRangeStart, tRangeEnd, groupSize, nbThread, keys, p);
	//getEvenlyDistributedGPUStartingKeys(rndGPU, tRangeStart, tRangeEnd, groupSize, nbThread, keys, p);
	getOverSampledGPUStartingKeys(rndGPU, tRangeStart, tRangeEnd, groupSize, nbThread, keys, p);

	ok = g->SetKeys(p);

	ph->hasStarted = true;
	ph->rKeyRequest = false;


	// GPU Thread
	while (ok && !endOfSearch) {

		if (ph->rKeyRequest) {	
			getOverSampledGPUStartingKeys(rndGPU, tRangeStart, tRangeEnd, groupSize, nbThread, keys, p);
			//getEvenlyDistributedGPUStartingKeys(rndGPU, tRangeStart, tRangeEnd, groupSize, nbThread, keys, p);
			//getGPUStartingKeysViaThrust(rndGPU, tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, p);
			//getGPUStartingKeys2(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, p);
			ok = g->SetKeys(p);
			ph->rKeyRequest = false;
		}

		// Call kernel
		switch (searchMode) {
		case (int)SEARCH_MODE_MA:
			ok = g->LaunchSEARCH_MODE_MA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_MX:
			ok = g->LaunchSEARCH_MODE_MX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		case (int)SEARCH_MODE_SA:
			ok = g->LaunchSEARCH_MODE_SA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_SX:
			ok = g->LaunchSEARCH_MODE_SX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		default:
			break;
		}

		//Int stepSize;
		//stepSize.SetInt64(1024*2);

		if (ok) {
			for (int i = 0; i < nbThread; i++) {
				//keys[i].Add(&stepSize);
				keys[i].Add((uint64_t)STEP_SIZE);
			}
			//counters[thId] += stepSize.Mult(nbThread); // Point
			counters[thId] += (uint64_t)(STEP_SIZE)*nbThread; // Point
		}

	}

	delete[] keys;
	delete[] p;
	delete g;

#else
	ph->hasStarted = true;
	printf("  GPU code not compiled, use -DWITHGPU when compiling.\n");
#endif

	ph->isRunning = false;

}

void KeyFinder::FindKeyGPUX(TH_PARAM* ph)
{

	bool ok = true;

#ifdef WITHGPU

	// Global init
	int thId = ph->threadId;
	Int tRangeStart = ph->rangeStart;
	Int tRangeEnd = ph->rangeEnd;

	GPUEngine* g;
	switch (searchMode) {
	case (int)SEARCH_MODE_MA:
	case (int)SEARCH_MODE_MX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			BLOOM_N, bloom->get_bits(), bloom->get_hashes(), bloom->get_bf(), DATA, TOTAL_COUNT, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SA:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			hash160Keccak, (rKey != 0));
		break;
	case (int)SEARCH_MODE_SX:
		g = new GPUEngine(secp, ph->gridSizeX, ph->gridSizeY, ph->gpuId, maxFound, searchMode, compMode, coinType,
			xpoint, (rKey != 0));
		break;
	default:
		printf("  Invalid search mode format!");
		return;
		break;
	}


	int nbThread = g->GetNbThread();
	Point* p = new Point[nbThread];
	Int* keys = new Int[nbThread];
	std::vector<std::vector<Int>>* keyparts = new std::vector<std::vector<Int>>();
	std::vector<ITEM> found;

	printf("  GPU          : %s\n", g->deviceName.c_str());

	counters[thId] = 0;

	getGPUStartingKeysAndParts(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, keyparts, p);
	
	ok = g->SetKeys(p);

	ph->hasStarted = true;
	ph->rKeyRequest = false;

	// GPU Thread
	while (ok && !endOfSearch) {

		if (ph->rKeyRequest) {
			getGPUStartingKeys2(tRangeStart, tRangeEnd, g->GetGroupSize(), nbThread, keys, p);
			ok = g->SetKeys(p);
			ph->rKeyRequest = false;
		}

		// Call kernel
		switch (searchMode) {
		case (int)SEARCH_MODE_MA:
			ok = g->LaunchSEARCH_MODE_MA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_MX:
			ok = g->LaunchSEARCH_MODE_MX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		case (int)SEARCH_MODE_SA:
			ok = g->LaunchSEARCH_MODE_SA(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				if (coinType == COIN_BTC) {
					std::string addr = secp->GetAddress(it.mode, it.hash);
					if (checkPrivKey(addr, keys[it.thId], it.incr, it.mode)) {
						nbFoundKey++;
					}
				}
				else {
					std::string addr = secp->GetAddressETH(it.hash);
					if (checkPrivKeyETH(addr, keys[it.thId], it.incr)) {
						nbFoundKey++;
					}
				}
			}
			break;
		case (int)SEARCH_MODE_SX:
			ok = g->LaunchSEARCH_MODE_SX(found, false);
			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {
				ITEM it = found[i];
				//Point pk;
				//memcpy((uint32_t*)pk.x.bits, (uint32_t*)it.hash, 8);
				//string addr = secp->GetAddress(it.mode, pk);
				if (checkPrivKeyX(/*addr,*/ keys[it.thId], it.incr, it.mode)) {
					nbFoundKey++;
				}
			}
			break;
		default:
			break;
		}

		//Int stepSize;
		//stepSize.SetInt64(1024*2);

		if (ok) {
			for (int i = 0; i < nbThread; i++) {
				//keys[i].Add(&stepSize);
				keys[i].Add((uint64_t)STEP_SIZE);
			}
			//counters[thId] += stepSize.Mult(nbThread); // Point
			counters[thId] += (uint64_t)(STEP_SIZE)*nbThread; // Point
		}

	}

	delete[] keys;
	delete[] p;
	delete g;

#else
	ph->hasStarted = true;
	printf("  GPU code not compiled, use -DWITHGPU when compiling.\n");
#endif

	ph->isRunning = false;

}

// ----------------------------------------------------------------------------

bool KeyFinder::isAlive(TH_PARAM * p)
{

	bool isAlive = true;
	int total = nbCPUThread + nbGPUThread;
	for (int i = 0; i < total; i++)
		isAlive = isAlive && p[i].isRunning;

	return isAlive;

}

// ----------------------------------------------------------------------------

bool KeyFinder::hasStarted(TH_PARAM * p)
{

	bool hasStarted = true;
	int total = nbCPUThread + nbGPUThread;
	for (int i = 0; i < total; i++)
		hasStarted = hasStarted && p[i].hasStarted;

	return hasStarted;

}

// ----------------------------------------------------------------------------

uint64_t KeyFinder::getGPUCount()
{
	uint64_t count = 0;
	if (value777 > 1000000) {
		count = value777;
	}

	for (int i = 0; i < nbGPUThread; i++)
		count += counters[0x80L + i];
	return count;

}

// ----------------------------------------------------------------------------

uint64_t KeyFinder::getCPUCount()
{

	uint64_t count = 0;
	for (int i = 0; i < nbCPUThread; i++)
		count += counters[i];
	return count;

}

// ----------------------------------------------------------------------------

void KeyFinder::rKeyRequest(TH_PARAM * p) {

	int total = nbCPUThread + nbGPUThread;
	for (int i = 0; i < total; i++)
		p[i].rKeyRequest = true;

}
// ----------------------------------------------------------------------------

void KeyFinder::SetupRanges(uint32_t totalThreads)
{
	Int threads;
	threads.SetInt32(totalThreads);
	rangeDiff.Set(&rangeEnd);
	rangeDiff.Sub(&rangeStart);
	rangeDiff.Div(&threads);
}

// ----------------------------------------------------------------------------

void KeyFinder::Search(int nbThread, std::vector<int> gpuId, std::vector<int> gridSize, bool& should_exit)
{

	double t0;
	double t1;
	endOfSearch = false;
	nbCPUThread = nbThread;
	nbGPUThread = (useGpu ? (int)gpuId.size() : 0);
	nbFoundKey = 0;

	// setup ranges
	SetupRanges(nbCPUThread + nbGPUThread);

	memset(counters, 0, sizeof(counters));

	if (!useGpu)
		printf("\n");

	TH_PARAM* params = (TH_PARAM*)malloc((nbCPUThread + nbGPUThread) * sizeof(TH_PARAM));
	memset(params, 0, (nbCPUThread + nbGPUThread) * sizeof(TH_PARAM));

	// Launch CPU threads
	for (int i = 0; i < nbCPUThread; i++) {
		params[i].obj = this;
		params[i].threadId = i;
		params[i].isRunning = true;
		if (rKey > 0) {
			Int kubik;
			params[i].rangeStart.Set(&kubik);
		}
		else {
			params[i].rangeStart.Set(&rangeStart);
			rangeStart.Add(&rangeDiff);
			params[i].rangeEnd.Set(&rangeStart);
		}
		

#ifdef WIN64
		DWORD thread_id;
		CreateThread(NULL, 0, _FindKeyCPU, (void*)(params + i), 0, &thread_id);
		ghMutex = CreateMutex(NULL, FALSE, NULL);
#else
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, &_FindKeyCPU, (void*)(params + i));
		ghMutex = PTHREAD_MUTEX_INITIALIZER;
#endif
	}

	// Launch GPU threads
	for (int i = 0; i < nbGPUThread; i++) {
		params[nbCPUThread + i].obj = this;
		params[nbCPUThread + i].threadId = 0x80L + i;
		params[nbCPUThread + i].isRunning = true;
		params[nbCPUThread + i].gpuId = gpuId[i];
		params[nbCPUThread + i].gridSizeX = gridSize[2 * i];
		params[nbCPUThread + i].gridSizeY = gridSize[2 * i + 1];
		if (rKey > 0) {
			Int kubik;
			params[nbCPUThread + i].rangeStart.Set(&kubik);
		}
		else {
			params[nbCPUThread + i].rangeStart.Set(&rangeStart);
			rangeStart.Add(&rangeDiff);
			params[nbCPUThread + i].rangeEnd.Set(&rangeStart);
		}
		


#ifdef WIN64
		DWORD thread_id;
		CreateThread(NULL, 0, _FindKeyGPU, (void*)(params + (nbCPUThread + i)), 0, &thread_id);
#else
		pthread_t thread_id;
		pthread_create(&thread_id, NULL, &_FindKeyGPU, (void*)(params + (nbCPUThread + i)));
#endif
	}

#ifndef WIN64
	setvbuf(stdout, NULL, _IONBF, 0);
#endif
	printf("\n");

	uint64_t lastCount = 0;
	uint64_t gpuCount = 0;
	uint64_t lastGPUCount = 0;

	// Key rate smoothing filter
#define FILTER_SIZE 8
	double lastkeyRate[FILTER_SIZE];
	double lastGpukeyRate[FILTER_SIZE];
	uint32_t filterPos = 0;

	double keyRate = 0.0;
	double gpuKeyRate = 0.0;
	char timeStr[256];

	memset(lastkeyRate, 0, sizeof(lastkeyRate));
	memset(lastGpukeyRate, 0, sizeof(lastkeyRate));

	// Wait that all threads have started
	while (!hasStarted(params)) {
		Timer::SleepMillis(500);
	}

	// Reset timer
	Timer::Init();
	t0 = Timer::get_tick();
	startTime = t0;
	Int p100;
	Int ICount;
	p100.SetInt32(100);
	double completedPerc = 0;
	uint64_t rKeyCount = 0;
	while (isAlive(params)) {

		int delay = 1000;
		while (isAlive(params) && delay > 0) {
			Timer::SleepMillis(500);
			delay -= 500;
		}

		gpuCount = getGPUCount();
		uint64_t count = getCPUCount() + gpuCount;
		ICount.SetInt64(count);
		int completedBits = ICount.GetBitLength();
		if (rKey <= 0) {
			completedPerc = CalcPercantage(ICount, rangeStart, rangeDiff2);
			//ICount.Mult(&p100);
			//ICount.Div(&this->rangeDiff2);
			//completedPerc = std::stoi(ICount.GetBase10());
		}
		minuty++;

		if (next > 0) {
			if (rKey < 1) {
				if (minuty > next * 55) {

					char* ctimeBuff;
					time_t now = time(NULL);
					ctimeBuff = ctime(&now);
					FILE* ptrFile = fopen("KeyScanner_Continue.bat", "w+");
					fprintf(ptrFile, ":loop \n");
					fprintf(ptrFile, "%s\n", stroka.c_str());
					fprintf(ptrFile, "goto :loop \n");
					fprintf(ptrFile, "created: %s", ctimeBuff);
					fprintf(ptrFile, "%" PRIu64 "\n", count);
					fprintf(ptrFile, "To continue correctly, DO NOT change the parameters in this file! \n");
					fprintf(ptrFile, "If you no longer need the continuation, DELETE this file! \n");
					fclose(ptrFile);
					minuty = 0;
				}
			}
		}

		t1 = Timer::get_tick();
		keyRate = (double)(count - lastCount) / (t1 - t0);
		gpuKeyRate = (double)(gpuCount - lastGPUCount) / (t1 - t0);
		lastkeyRate[filterPos % FILTER_SIZE] = keyRate;
		lastGpukeyRate[filterPos % FILTER_SIZE] = gpuKeyRate;
		filterPos++;

		// KeyRate smoothing
		double avgKeyRate = 0.0;
		double avgGpuKeyRate = 0.0;
		uint32_t nbSample;
		for (nbSample = 0; (nbSample < FILTER_SIZE) && (nbSample < filterPos); nbSample++) {
			avgKeyRate += lastkeyRate[nbSample];
			avgGpuKeyRate += lastGpukeyRate[nbSample];
		}
		avgKeyRate /= (double)(nbSample);
		avgGpuKeyRate /= (double)(nbSample);

		zhdat++;

		unsigned long long int years88=0, days88=0, hours88=0, minutes88=0, seconds88 = 0;

		if (nbit2 > 0) {
			
			if (rKey < 1) {

				if (value777 > 1000000) {

					if (zhdat > 10) {
						double avgKeyRate2 = avgKeyRate * 1;
						rhex.Add(avgKeyRate2);
						double avgKeyRate5 = avgKeyRate * 1;
						unsigned long long int input88;
						unsigned long long int input55;
						unsigned long long int minnn;
						string streek77 = rangeDiffbar.GetBase10().c_str();
						std::istringstream iss(streek77);
						iss >> input55;
						minnn = input55 - count;
						input88 = minnn / avgKeyRate5;
						years88 = input88 / 60 / 60 / 24 / 365;
						days88 = (input88 / 60 / 60 / 24) % 365;
						hours88 = (input88 / 60 / 60) % 24;
						minutes88 = (input88 / 60) % 60;
						seconds88 = input88 % 60;
					}
				}
				else {
					double avgKeyRate2 = avgKeyRate * 1;
					rhex.Add(avgKeyRate2);
					double avgKeyRate5 = avgKeyRate * 1;
					unsigned long long int input88;
					unsigned long long int input55;
					unsigned long long int minnn;
					string streek77 = rangeDiffbar.GetBase10().c_str();
					std::istringstream iss(streek77);
					iss >> input55;
					minnn = input55 - count;
					input88 = minnn / avgKeyRate5;
					years88 = input88 / 60 / 60 / 24 / 365;
					days88 = (input88 / 60 / 60 / 24) % 365;
					hours88 = (input88 / 60 / 60) % 24;
					minutes88 = (input88 / 60) % 60;
					seconds88 = input88 % 60;
				}
			}
			else {

				double avgKeyRate2 = avgKeyRate * 1;
				double avgKeyRate3 = avgKeyRate2 / nbit2;
				rhex.Add(avgKeyRate3);

			}
		}
		else {

			if (rKey < 1) {

				if (value777 > 1000000) {

					if (zhdat > 10) {
						double avgKeyRate2 = avgKeyRate * 1;
						rhex.Add(avgKeyRate2);
						double avgKeyRate5 = avgKeyRate * 1;
						unsigned long long int input88;
						unsigned long long int input55;
						unsigned long long int minnn;
						string streek77 = rangeDiffbar.GetBase10().c_str();
						std::istringstream iss(streek77);
						iss >> input55;
						minnn = input55 - count;
						input88 = minnn / avgKeyRate5;
						years88 = input88 / 60 / 60 / 24 / 365;
						days88 = (input88 / 60 / 60 / 24) % 365;
						hours88 = (input88 / 60 / 60) % 24;
						minutes88 = (input88 / 60) % 60;
						seconds88 = input88 % 60;
					}
				}
				else {
					double avgKeyRate2 = avgKeyRate * 1;
					rhex.Add(avgKeyRate2);
					double avgKeyRate5 = avgKeyRate * 1;
					unsigned long long int input88;
					unsigned long long int input55;
					unsigned long long int minnn;
					string streek77 = rangeDiffbar.GetBase10().c_str();
					std::istringstream iss(streek77);
					iss >> input55;
					minnn = input55 - count;
					input88 = minnn / avgKeyRate5;
					years88 = input88 / 60 / 60 / 24 / 365;
					days88 = (input88 / 60 / 60 / 24) % 365;
					hours88 = (input88 / 60 / 60) % 24;
					minutes88 = (input88 / 60) % 60;
					seconds88 = input88 % 60;
				}
			}
			else {

				double avgKeyRate2 = avgKeyRate * 1;
				double avgKeyRate3 = avgKeyRate2 / gpucores;
				rhex.Add(avgKeyRate3);

			}
		}
		
		//I dunno why this was done this way...   need to refactor it but I try not to pay attention to it b/c it tweaks my refactor OCD tendancies
		if (years88 > 300) {

			if (display > 0) {

				if (years88 > 0) {

					if (nbit2 < 1) {

						if (rKey > 0) {

							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							string skoka = "";
							if (avgGpuKeyRate > 1000000000) {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										completedPerc,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										completedPerc,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
					else {

						if (rKey > 0) {
							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							if (avgGpuKeyRate > 1000000000) {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
				}
				else {

					if (days88 > 0) {

						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
					else {
						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
				}
			}
			else {

				if (years88 > 0) {

					if (nbit2 < 1) {

						if (rKey > 0) {

							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							string skoka = "";
							if (avgGpuKeyRate > 1000000000) {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										completedPerc,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										completedPerc,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
					else {

						if (rKey > 0) {
							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							if (avgGpuKeyRate > 1000000000) {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
				}
				else {

					if (days88 > 0) {

						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
					else {
						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
				}
			}

		}
		else {
			if (display > 0) {

				if (years88 > 0) {

					if (nbit2 < 1) {

						if (rKey > 0) {

							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							string skoka = "";
							if (avgGpuKeyRate > 1000000000) {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
					else {

						if (rKey > 0) {
							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										rhex.GetBase16().c_str(),
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							if (avgGpuKeyRate > 1000000000) {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										rhex.GetBase16().c_str(),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
				}
				else {

					if (days88 > 0) {

						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
					else {
						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [%s] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											rhex.GetBase16().c_str(),
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											rhex.GetBase16().c_str(),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
				}
			}
			else {

				if (years88 > 0) {

					if (nbit2 < 1) {

						if (rKey > 0) {

							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							string skoka = "";
							if (avgGpuKeyRate > 1000000000) {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										avgGpuKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {

								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										avgGpuKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
					else {

						if (rKey > 0) {
							if (isAlive(params)) {

								if (avgGpuKeyRate > 1000000000) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
								else {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]   ",
										toTimeStr(t1, timeStr),
										rKeyCount,
										nbFoundKey,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
						else {

							if (avgGpuKeyRate > 1000000000) {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000000.0,
										formatThousands(count).c_str());
								}
							}
							else {
								if (isAlive(params)) {
									memset(timeStr, '\0', 256);
									printf("\r  [%s] [F: %d] [Y:%03llu D:%03llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
										toTimeStr(t1, timeStr),
										nbFoundKey,
										years88,
										days88,
										completedPerc,
										nbit2,
										avgKeyRate / 1000000.0,
										formatThousands(count).c_str());
								}
							}
						}
					}
				}
				else {

					if (days88 > 0) {

						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [D:%03llu %02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											days88,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
					else {
						if (nbit2 < 1) {

							if (rKey > 0) {

								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [GPU: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								string skoka = "";
								if (avgGpuKeyRate > 1000000000) {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Gk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {

									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [GPU: %.2f Mk/s] [T: %s]   ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											avgGpuKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
						else {

							if (rKey > 0) {
								if (isAlive(params)) {

									if (avgGpuKeyRate > 1000000000) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Gk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
									else {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [R: %llu] [F: %d] [CPU %d: %.2f Mk/s] [T: %s]    ",
											toTimeStr(t1, timeStr),
											rKeyCount,
											nbFoundKey,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
							else {

								if (avgGpuKeyRate > 1000000000) {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Gk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000000.0,
											formatThousands(count).c_str());
									}
								}
								else {
									if (isAlive(params)) {
										memset(timeStr, '\0', 256);
										printf("\r  [%s] [F: %d] [%02llu:%02llu:%02llu] [C: %lf %%] [CPU %d: %.2f Mk/s] [T: %s]  ",
											toTimeStr(t1, timeStr),
											nbFoundKey,
											hours88,
											minutes88,
											seconds88,
											completedPerc,
											nbit2,
											avgKeyRate / 1000000.0,
											formatThousands(count).c_str());
									}
								}
							}
						}
					}
				}
			}
		}
		
		uint64_t rKeyMultiplier = 1000000000; // 250000000;// 1000000000;// 32000000;//65536;// 1000000000;
		if (rKey > 0) {
			if ((count - lastrKey) > (rKeyMultiplier * rKey)) {
			// if ((count - lastrKey) > (6000000 * rKey)) { // a stranger on the internet said do this, what could go wrng?
				// rKey request
				rKeyRequest(params);
				lastrKey = count;
				rKeyCount++;
				rKeyCount2 += rKeyCount;
			}
		}

		lastCount = count;
		lastGPUCount = gpuCount;
		t0 = t1;
		if (should_exit || nbFoundKey >= targetCounter || completedPerc > 100.5  || (maxSeconds > 0 && t1 > maxSeconds))
			endOfSearch = true;
	}

	free(params);

	}

// ----------------------------------------------------------------------------

std::string KeyFinder::GetHex(std::vector<unsigned char> &buffer)
{
	std::string ret;

	char tmp[128];
	for (int i = 0; i < (int)buffer.size(); i++) {
		sprintf(tmp, "%02X", buffer[i]);
		ret.append(tmp);
	}
	return ret;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

int KeyFinder::CheckBloomBinary(const uint8_t * _xx, uint32_t K_LENGTH)
{
	if (bloom->check(_xx, K_LENGTH) > 0) {
		uint8_t* temp_read;
		uint64_t half, min, max, current; //, current_offset
		int64_t rcmp;
		int32_t r = 0;
		min = 0;
		current = 0;
		max = TOTAL_COUNT;
		half = TOTAL_COUNT;
		while (!r && half >= 1) {
			half = (max - min) / 2;
			temp_read = DATA + ((current + half) * K_LENGTH);
			rcmp = memcmp(_xx, temp_read, K_LENGTH);
			if (rcmp == 0) {
				r = 1;  //Found!!
			}
			else {
				if (rcmp < 0) { //data < temp_read
					max = (max - half);
				}
				else { // data > temp_read
					min = (min + half);
				}
				current = min;
			}
		}
		return r;
	}
	return 0;
}

// ----------------------------------------------------------------------------

bool KeyFinder::MatchHash(uint32_t * _h)
{
	if (_h[0] == hash160Keccak[0] &&
		_h[1] == hash160Keccak[1] &&
		_h[2] == hash160Keccak[2] &&
		_h[3] == hash160Keccak[3] &&
		_h[4] == hash160Keccak[4]) {
		return true;
	}
	else {
		return false;
	}
}

// ----------------------------------------------------------------------------

bool KeyFinder::MatchXPoint(uint32_t * _h)
{
	if (_h[0] == xpoint[0] &&
		_h[1] == xpoint[1] &&
		_h[2] == xpoint[2] &&
		_h[3] == xpoint[3] &&
		_h[4] == xpoint[4] &&
		_h[5] == xpoint[5] &&
		_h[6] == xpoint[6] &&
		_h[7] == xpoint[7]) {
		return true;
	}
	else {
		return false;
	}
}

// ----------------------------------------------------------------------------

std::string KeyFinder::formatThousands(uint64_t x)
{
	char buf[32] = "";

	sprintf(buf, "%llu", x);

	std::string s(buf);

	int len = (int)s.length();

	int numCommas = (len - 1) / 3;

	if (numCommas == 0) {
		return s;
	}

	std::string result = "";

	int count = ((len % 3) == 0) ? 0 : (3 - (len % 3));

	for (int i = 0; i < len; i++) {
		result += s[i];

		if (count++ == 2 && i < len - 1) {
			result += ",";
			count = 0;
		}
	}
	return result;
}


// ----------------------------------------------------------------------------

char* KeyFinder::toTimeStr(int sec, char* timeStr)
{
	int h, m, s;
	h = (sec / 3600);
	m = (sec - (3600 * h)) / 60;
	s = (sec - (3600 * h) - (m * 60));
	sprintf(timeStr, "%0*d:%0*d:%0*d", 2, h, 2, m, 2, s);
	return (char*)timeStr;
}

// ----------------------------------------------------------------------------

//#include <gmp.h>
//#include <gmpxx.h>
// ((input - min) * 100) / (max - min)
//double KeyFinder::GetPercantage(uint64_t v)
//{
//	//Int val(v);
//	//mpz_class x(val.GetBase16().c_str(), 16);
//	//mpz_class r(rangeStart.GetBase16().c_str(), 16);
//	//x = x - mpz_class(rangeEnd.GetBase16().c_str(), 16);
//	//x = x * 100;
//	//mpf_class y(x);
//	//y = y / mpf_class(r);
//	return 0;// y.get_d();
//}
