#ifndef _UTIL_H
#define _UTIL_H

#include <string>
#include <vector>
#include <stdint.h>
#include "../Secp256k1/secp256k1.h"

namespace util {

	class Timer {

	private:
		uint64_t _startTime;

	public:
		Timer();
		void start();
		uint64_t getTime();
	};

	uint64_t getSystemTime();
	void sleep(int seconds);

	std::vector<std::string> split(const std::string& s, char delim);
	std::string tolower(std::string str);

	std::string formatThousands(uint64_t x);
	std::string formatSeconds(unsigned int seconds);
	std::string formatDecimal(float v);
	std::string GetTimeStr(double dTime);

	uint32_t parseUInt32(std::string s);
	uint64_t parseUInt64(std::string s);
	bool isHex(const std::string& s);
	bool appendToFile(const std::string& fileName, const std::string& s);
	bool readLinesFromStream(std::istream& in, std::vector<std::string>& lines);
	bool readLinesFromStream(const std::string& fileName, std::vector<std::string>& lines);



	std::string format(const char* formatStr, double value);
	std::string format(uint32_t value);
	std::string format(uint64_t value);
	std::string format(int value);
	void removeNewline(std::string& s);
	unsigned int endian(unsigned int x);
	unsigned int reverse(unsigned int x);

	std::string toLower(const std::string& s);
	std::string trim(const std::string& s, char c = ' ');
	std::string occurrencesOf(const std::string& s, char c = ' ');

	std::vector<secp256k1::StrideMapEntry> parseStrideFile(std::string stridesFile);

}

#endif