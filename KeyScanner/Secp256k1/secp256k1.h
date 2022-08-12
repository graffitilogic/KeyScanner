#ifndef _HOST_SECP256K1_H
#define _HOST_SECP256K1_H

#include<stdio.h>
#include<stdint.h>
#include<string.h>
#include<string>
#include<vector>
#include<random>
#include<cuda.h>
#include<curand.h>

#define CUDA_CALL(x) do { if((x) != cudaSuccess) { \
      printf("Error at %s:%d\n",__FILE__,__LINE__);     \
      return EXIT_FAILURE;}} while(0)
#define CURAND_CALL(x) do { if((x) != CURAND_STATUS_SUCCESS) { \
      printf("Error at %s:%d\n",__FILE__,__LINE__);            \
      return EXIT_FAILURE;}} while(0)

//#include<thrust/random.h>

namespace secp256k1 {

	class CPURandomMt1993
	{
		std::random_device rd;
		std::mt19937* gen;
		std::uniform_int_distribution<unsigned int>* distr;

	public:

		CPURandomMt1993()
		{
			gen = new std::mt19937(rd());
			distr = new std::uniform_int_distribution<unsigned int>(0, 0xffffffff);
		}

		unsigned int getChunk() {
			return (*distr)(*gen);
		}

		~CPURandomMt1993() {
			delete gen;
			delete distr;
		}
	};

	class CPURandomDefault
	{
		std::random_device rd;
		std::default_random_engine* gen;
		std::uniform_int_distribution<unsigned int>* distr;

	public:

		CPURandomDefault()
		{
			gen = new std::default_random_engine(rd());
			distr = new std::uniform_int_distribution<unsigned int>(0, 0xffffffff);
		}

		unsigned int getChunk() {
			return (*distr)(*gen);
		}

		~CPURandomDefault() {
			delete gen;
			delete distr;
		}
	};

	class GPURandomDefault
	{
		std::vector<unsigned int> rand_buffer;
		uint64_t buffer_max;
		uint64_t buffer_index;

	public:

		GPURandomDefault()
		{

		}

		void setBuffer(std::vector<unsigned int> buff) {
			rand_buffer = buff;
			buffer_max = rand_buffer.size() - 1;
		}

		unsigned int getChunk() {
			if (buffer_index == buffer_max) {
				buffer_index = 0;
			}
			else {
				buffer_index++;
			}

			return rand_buffer.at(buffer_index);
		}

		~GPURandomDefault() {
			//delete rand_buffer;
		}
	};




	class uint256 {

	public:
		static const int BigEndian = 1;
		static const int LittleEndian = 2;

		uint32_t v[8] = { 0 };

		uint256()
		{
			memset(this->v, 0, sizeof(this->v));
		}

		uint256(const std::string& s)
		{
			std::string t = s;

			// 0x prefix
			if (t.length() >= 2 && (t[0] == '0' && t[1] == 'x' || t[1] == 'X')) {
				t = t.substr(2);
			}

			// 'h' suffix
			if (t.length() >= 1 && t[t.length() - 1] == 'h') {
				t = t.substr(0, t.length() - 1);
			}

			if (t.length() == 0) {
				throw std::string("Incorrect hex formatting");
			}

			// Verify only valid hex characters
			for (int i = 0; i < (int)t.length(); i++) {
				if (!((t[i] >= 'a' && t[i] <= 'f') || (t[i] >= 'A' && t[i] <= 'F') || (t[i] >= '0' && t[i] <= '9'))) {
					throw std::string("Incorrect hex formatting");
				}
			}

			// Ensure the value is 64 hex digits. If it is longer, take the least-significant 64 hex digits.
			// If shorter, pad with 0's.
			if (t.length() > 64) {
				t = t.substr(t.length() - 64);
			}
			else if (t.length() < 64) {
				t = std::string(64 - t.length(), '0') + t;
			}

			int len = (int)t.length();

			memset(this->v, 0, sizeof(uint32_t) * 8);

			int j = 0;
			for (int i = len - 8; i >= 0; i -= 8) {
				std::string sub = t.substr(i, 8);
				uint32_t val;
				if (sscanf(sub.c_str(), "%x", &val) != 1) {
					throw std::string("Incorrect hex formatting");
				}
				this->v[j] = val;
				j++;
			}
		}

		uint256(unsigned int x)
		{
			memset(this->v, 0, sizeof(this->v));
			this->v[0] = x;
		}

		uint256(uint64_t x)
		{
			memset(this->v, 0, sizeof(this->v));
			this->v[0] = (unsigned int)x;
			this->v[1] = (unsigned int)(x >> 32);
		}

		uint256(int x)
		{
			memset(this->v, 0, sizeof(this->v));
			this->v[0] = (unsigned int)x;
		}

		uint256(const unsigned int x[8], int endian = LittleEndian)
		{
			if (endian == LittleEndian) {
				for (int i = 0; i < 8; i++) {
					this->v[i] = x[i];
				}
			}
			else {
				for (int i = 0; i < 8; i++) {
					this->v[i] = x[7 - i];
				}
			}
		}

		bool operator==(const uint256& x) const
		{
			for (int i = 0; i < 8; i++) {
				if (this->v[i] != x.v[i]) {
					return false;
				}
			}

			return true;
		}

		//add operators for natural sorting and comparison.  Why were these two not already there?
		bool operator<(const uint256& val) const
		{
			return cmp(val) == -1;
		}

		bool operator>(const uint256& val) {
			return cmp(val) == 1;
		}

		uint256 operator+(const uint256& x) const
		{
			return add(x);
		}

		uint256 operator+(uint32_t x) const
		{
			return add(x);
		}

		uint256 operator*(uint32_t x) const
		{
			return mul(x);
		}

		uint256 operator*(const uint256& x) const
		{
			return mul(x);
		}

		uint256 operator*(uint64_t x) const
		{
			return mul(x);
		}

		uint256 operator-(const uint256& x) const
		{
			return sub(x);
		}

		void exportWords(unsigned int* buf, int len, int endian = LittleEndian) const
		{
			if (endian == LittleEndian) {
				for (int i = 0; i < len; i++) {
					buf[i] = v[i];
				}
			}
			else {
				for (int i = 0; i < len; i++) {
					buf[len - i - 1] = v[i];
				}
			}
		}

		uint256 rShift(uint32_t val) const;

		uint256 mul(const uint256& val) const;

		uint256 mul(int val) const;

		uint256 mul(uint32_t val) const;

		uint256 mul(uint64_t val) const;

		uint256 add(int val) const;

		uint256 add(unsigned int val) const;

		uint256 add(uint64_t val) const;

		uint256 sub(int val) const;

		uint256 sub(const uint256& val) const;

		uint256 add(const uint256& val) const;

		uint256 div(uint32_t val) const;

		uint256 div(const uint256& val) const;

		uint256 mod(uint32_t val) const;

		unsigned int toInt32() const
		{
			return this->v[0];
		}
		unsigned long long int toInt64() const
		{
			return this->v[0] | (unsigned long long int)this->v[1] << 32;
		}

		bool isZero() const
		{
			for (int i = 0; i < 8; i++) {
				if (this->v[i] != 0) {
					return false;
				}
			}

			return true;
		}

		int cmp(const uint256& val) const
		{
			for (int i = 7; i >= 0; i--) {

				if (this->v[i] < val.v[i]) {
					// less than
					return -1;
				}
				else if (this->v[i] > val.v[i]) {
					// greater than
					return 1;
				}
			}

			// equal
			return 0;
		}

		int cmp(unsigned int& val) const
		{
			// If any higher bits are set then it is greater
			for (int i = 7; i >= 1; i--) {
				if (this->v[i]) {
					return 1;
				}
			}

			if (this->v[0] > val) {
				return 1;
			}
			else if (this->v[0] < val) {
				return -1;
			}

			return 0;
		}

		uint256 pow(int n)
		{
			uint256 product(1);
			uint256 square = *this;

			while (n) {
				if (n & 1) {
					product = product.mul(square);
				}
				square = square.mul(square);

				n >>= 1;
			}

			return product;
		}

		int length() {
			int len = 0;
			uint32_t sixteen = 16;
			uint256 v = *this;
			while (v > 0) {
				v = v.div(sixteen);
				len++;
			}
			return len;
		}

		uint256 ltrim(int preserved_places) {
			uint32_t sixteen = 16;
			uint256 val = *this;

			uint64_t len = val.length();

			secp256k1::uint256 subtractor = 1;
			for (int k = 0; k < len - preserved_places; k++) {
				val = val.div(sixteen);
				subtractor = subtractor.mul(sixteen);
			}

			subtractor = subtractor * val;
			secp256k1::uint256 newKey = *this;
			newKey = newKey.sub(subtractor);

			return newKey;
		}

		uint256 left(int preserved_places) {
			uint32_t sixteen = 16;
			uint256 val = *this;

			int len = val.length();

			for (int k = 0; k < len - preserved_places; k++) {
				val = val.div(sixteen);
			}

			return val;
		}

		std::vector<uint256> vectorize() {
			std::vector<uint256> results;

			uint256 w = *this;
			int lastLen = w.length();
			while (w.length() > 0) {
				uint256 v;
				if (lastLen - w.length() > 1) { //if a zero was taken in the trim()
					v = 0;
				}
				else {
					v = w.left(1);
				}
				results.push_back(v);
				lastLen = w.length();
				if (v > 0) w = w.ltrim(1);
			}
			return results;
		}

		std::vector<uint256> vectorizeStr() {
			std::vector<uint256> results;
			uint256 v = *this;

			std::string s = v.toString();
			for (int i = 0; i < s.size(); i++) {
				results.push_back(s[i]);
			}
			return results;
		}

		uint256 reverse() {

			uint256 src = *this;
			uint256 result = 0;

			for (int i = 0; i < 8; i++) {
				unsigned int v = src.v[i];

				unsigned int revX = 0;
				while (v > 0) {
					revX = revX * 10 + v % 10;
					v = v / 10;
				}

				result.v[i] = revX;
			}

			return uint256(result);
		}

		uint256 reverse_shallow() {

			uint256 num = *this;
			uint256 rev = 0;


			while (num > 0) {
				rev = rev.mul(16) + num.mod(16);
				num = num.div(16);
			}
			
			return rev;
		}

		uint256 reverse_deep() {

			uint256 src = *this;
			uint256 result = 0;

			for (int i = 0; i < 8; i++) {
				unsigned int v = src.v[i];

				unsigned int revX = 0;
				while (v > 0) {
					revX = revX * 10 + v % 10;
					v = v / 10;
				}

				result.v[i] = revX;
			}

			return uint256(result);
		}

		std::vector<uint256> getChunks() {
			uint64_t sixteen = 16;
			uint256 v = *this;
			std::vector<secp256k1::uint256> vv = v.vectorize();
			std::vector<secp256k1::uint256> results;

			while (vv.size() >= 4) {
				secp256k1::uint256 left = vv[0];
				for (int i = 1; i < 4; i++) {
					left = left * sixteen;
					left = left + vv[i];
				}
				//while (left.length() < 4) left = left.mul(sixteen);
				results.push_back(left);

				//write remaining back to vector
				std::vector<secp256k1::uint256> vr;
				for (int i = 4; i < vv.size(); i++) {

					vr.push_back(vv[i]);
				}


				vv.clear();

				if (vr.size() >= 4) {
					secp256k1::uint256 right = vr[vr.size() - 4];
					for (int i = vr.size() - 3; i < vr.size(); i++) {
						right = right * sixteen;
						right = right + vr[i];
					}
					//while (right.length() < 4) right = right.mul(sixteen);
					results.push_back(right);

					//write remaining back to vector
					vv.clear();
					for (int i = 0; i < vr.size() - 4; i++) {
						vv.push_back(vr[i]);
					}

				}

				if (vv.size() == 0 && vr.size() > 0) vv = vr;
			}


			secp256k1::uint256 remainder = 0;
			for (int i = 0; i < vv.size(); i++) {
				remainder = remainder * sixteen;
				remainder = remainder + vv[i];
			}

			if (remainder > 0) results.push_back(remainder);
			return results;
		}

		bool bit(int n)
		{
			n = n % 256;

			return (this->v[n / 32] & (0x1 << (n % 32))) != 0;
		}

		unsigned int getBitRange()
		{
			int ret = 0;
			for (int n = 255; n >= 0; n--) {
				if (bit(n)) {
					ret = n + 1;
					break;
				}
			}

			return ret;
		}

		bool isEven()
		{
			return (this->v[0] & 1) == 0;
		}

		std::string toString(int base = 16);

		uint64_t toUint64()
		{
			return ((uint64_t)this->v[1] << 32) | v[0];
		}
	};

	class StrideMapEntry {
	public:

		StrideMapEntry() {}
		uint64_t cycles = 0;
		uint256 stride;
		std::string strideStr;
		std::string directive;
		uint64_t strideQueueIndex = 0;
		uint64_t strideQueueMax = 0;

		StrideMapEntry emptyStride() {
			StrideMapEntry empty;
			empty.cycles = 0;
			empty.directive = "";
			empty.stride = 0;
			empty.strideQueueIndex = 0;
			empty.strideQueueMax = 0;
			empty.strideStr = "";
			return empty;
		}
	};


	class KeyPair {
	public:
		KeyPair() {
			keyA = 0;
			keyB = 0;
		}

		uint256 keyA;
		uint256 keyB;
	};

	class BetaKeyManipulator {
	public:
		BetaKeyManipulator() {
			keyA = 0;
			keyB = 0;
			expander = 0;
		}

		uint256 keyA;
		uint256 keyB;
		uint256 expander;
	};

	static CPURandomMt1993 rndCPUMt;
	static CPURandomDefault rndCPUDefault;

	static GPURandomDefault rndGPUDefault;

	//uint256 getRandomRange(uint256 min, uint256 max);
	uint256 getDefaultCPURandomRange(uint256 min, uint256 max);
	uint256 getDefaultCPURandomRangeExt(uint256 min, uint256 max, uint64_t max_repeats);

	uint256 getMTCPURandomRange(uint256 min, uint256 max);
	uint256 getDefaultCPURandomRange(uint256 min, uint256 max);


	std::vector<StrideMapEntry> makeStrideQueue(uint64_t cycles, uint256 baseStride, uint256 maxStride, uint64_t count);

	BetaKeyManipulator parseBetaKey(uint256 sampleKey, char placeHolder);

	uint256 getRandom128(int32_t bits, std::vector<uint32_t>& rStrideHistory);

	uint256 getRangeStart(int32_t len, std::string start);
	uint256 getRangeEnd(int32_t len, std::string start);

	std::vector<uint256> getSequentialRange(uint256 start, uint256 end, bool thinProvisioning);


	class KeyGenGuide {
	public:

		KeyGenGuide() {
			HexEntryIndex = 0;
			KeyLen = 0;
			MaxHexEntry = 0;
			MaxStringEntry = 0;
			StringEntryIndex = 0;
			isLiteral = false;
		}

		std::string KeyMask;
		uint256 keyMaskHex;

		uint64_t KeyLen;
		uint256 hexExpander;

		std::vector<uint256> HexEntries;
		uint64_t MaxHexEntry;
		uint64_t HexEntryIndex;

		std::vector<std::string> StringEntries;
		uint64_t MaxStringEntry;
		uint64_t StringEntryIndex;

		bool isLiteral;
		std::vector<uint64_t> IndexMap;
	};

	class KeyScaffold {
	public:

		KeyScaffold() {}

		std::string KeyMask;
		std::string LiteralMask;
		std::string SequentialMask;
		std::string RandomMask;

	};


	class KeyBuffers {
	public:

		KeyBuffers() {}

		std::vector<uint256> Literals;
		std::vector<uint256> Sequentials;
		std::vector<uint256> Randoms;
		std::vector<uint256> Keys;  
		std::vector<uint256> BetaKeys;
		std::vector<uint256> RootKeys;
		std::vector<uint256> Distances;
		//std::vector<std::vector<uint256>> Keys;  //keys[0] = first pass, keys[1] = 2nd pass and so on

		std::vector<std::vector<unsigned int>> Randomizers;


	};

	
	const unsigned int _POINT_AT_INFINITY_WORDS[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	const unsigned int _P_WORDS[8] = { 0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	const unsigned int _N_WORDS[8] = { 0xD0364141, 0xBFD25E8C, 0xAF48A03B, 0xBAAEDCE6, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
	const unsigned int _GX_WORDS[8] = { 0x16F81798, 0x59F2815B, 0x2DCE28D9, 0x029BFCDB, 0xCE870B07, 0x55A06295, 0xF9DCBBAC, 0x79BE667E };
	const unsigned int _GY_WORDS[8] = { 0xFB10D4B8, 0x9C47D08F, 0xA6855419, 0xFD17B448, 0x0E1108A8, 0x5DA4FBFC, 0x26A3C465, 0x483ADA77 };
	const unsigned int _BETA_WORDS[8] = { 0x719501EE, 0xC1396C28, 0x12F58995, 0x9CF04975, 0xAC3434E9, 0x6E64479E, 0x657C0710, 0x7AE96A2B };
	const unsigned int _LAMBDA_WORDS[8] = { 0x1B23BD72, 0xDF02967C, 0x20816678, 0x122E22EA, 0x8812645A, 0xA5261C02, 0xC05C30E0, 0x5363AD4C };


	class ecpoint {

	public:
		uint256 x;
		uint256 y;

		ecpoint()
		{
			this->x = uint256(_POINT_AT_INFINITY_WORDS);
			this->y = uint256(_POINT_AT_INFINITY_WORDS);
		}

		ecpoint(const uint256& x, const uint256& y)
		{
			this->x = x;
			this->y = y;
		}

		ecpoint(const ecpoint& p)
		{
			this->x = p.x;
			this->y = p.y;
		}

		ecpoint operator=(const ecpoint& p)
		{
			this->x = p.x;
			this->y = p.y;

			return *this;
		}

		bool operator==(const ecpoint& p) const
		{
			return this->x == p.x && this->y == p.y;
		}

		std::string toString(bool compressed = false)
		{
			if (!compressed) {
				return "04" + this->x.toString() + this->y.toString();
			}
			else {
				if (this->y.isEven()) {
					return "02" + this->x.toString();
				}
				else {
					return "03" + this->x.toString();
				}
			}
		}
	};

	const uint256 P(_P_WORDS);
	const uint256 N(_N_WORDS);

	const uint256 BETA(_BETA_WORDS);
	const uint256 LAMBDA(_LAMBDA_WORDS);

	ecpoint pointAtInfinity();
	ecpoint G();


	uint256 negModP(const uint256& x);
	uint256 negModN(const uint256& x);

	uint256 addModP(const uint256& a, const uint256& b);
	uint256 subModP(const uint256& a, const uint256& b);
	uint256 multiplyModP(const uint256& a, const uint256& b);
	uint256 multiplyModN(const uint256& a, const uint256& b);

	ecpoint addPoints(const ecpoint& p, const ecpoint& q);
	ecpoint doublePoint(const ecpoint& p);

	uint256 invModP(const uint256& x);

	bool isPointAtInfinity(const ecpoint& p);
	ecpoint multiplyPoint(const uint256& k, const ecpoint& p);

	uint256 addModN(const uint256& a, const uint256& b);
	uint256 subModN(const uint256& a, const uint256& b);

	uint256 generatePrivateKey();

	bool pointExists(const ecpoint& p);

	void generateKeyPairsBulk(unsigned int count, const ecpoint& basePoint, std::vector<uint256>& privKeysOut, std::vector<ecpoint>& pubKeysOut);
	void generateKeyPairsBulk(const ecpoint& basePoint, std::vector<uint256>& privKeys, std::vector<ecpoint>& pubKeysOut);

	ecpoint parsePublicKey(const std::string& pubKeyString);
}

#endif