#include<stdio.h>
#include<string>
#include<fstream>
#include<sstream>
#include<vector>
#include<set>
#include<algorithm>

#include "../Logger/Logger.h"
#include "../RandomHelper.h"
#include <chrono>

#include"util.h"

#ifdef _WIN32
#include<windows.h>
#else
#include<unistd.h>
#include<sys/stat.h>
#include<sys/time.h>
#include<libgen.h>
#endif

namespace util {

	uint64_t getSystemTime()
	{
#ifdef _WIN32
		return GetTickCount64();
#else
		struct timeval t;
		gettimeofday(&t, NULL);
		return (uint64_t)t.tv_sec * 1000 + t.tv_usec / 1000;
#endif
	}

	Timer::Timer()
	{
		_startTime = 0;
	}

	void Timer::start()
	{
		_startTime = getSystemTime();
	}

	uint64_t Timer::getTime()
	{
		return getSystemTime() - _startTime;
	}

	void sleep(int seconds)
	{
#ifdef _WIN32
		Sleep(seconds * 1000);
#else
		sleep(seconds);
#endif
	}

	std::vector<std::string> split(const std::string& s, char delim) {
		std::stringstream ss(s);
		std::string item;
		std::vector<std::string> tokens;
		while (getline(ss, item, delim)) {
			tokens.push_back(item);
		}
		return tokens;
	}

	std::string tolower(std::string str) {
		std::transform(str.begin(), str.end(), str.begin(), ::tolower);
		return str;
	}

	std::string formatThousands(uint64_t x)
	{
		char buf[32] = "";

		sprintf(buf, "%lld", x);

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

	uint32_t parseUInt32(std::string s)
	{
		return (uint32_t)parseUInt64(s);
	}

	uint64_t parseUInt64(std::string s)
	{
		uint64_t val = 0;
		bool isHex = false;

		if (s[0] == '0' && s[1] == 'x') {
			isHex = true;
			s = s.substr(2);
		}

		if (s[s.length() - 1] == 'h') {
			isHex = true;
			s = s.substr(0, s.length() - 1);
		}

		if (isHex) {
			if (sscanf(s.c_str(), "%llx", &val) != 1) {
				throw std::string("Expected an integer");
			}
		}
		else {
			if (sscanf(s.c_str(), "%lld", &val) != 1) {
				throw std::string("Expected an integer");
			}
		}

		return val;
	}

	bool isHex(const std::string& s)
	{
		int len = 0;

		for (int i = 0; i < len; i++) {
			char c = s[i];

			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
				return false;
			}
		}

		return true;
	}

	std::string formatSeconds(unsigned int seconds)
	{
		char s[128] = { 0 };

		unsigned int days = seconds / 86400;
		unsigned int hours = (seconds % 86400) / 3600;
		unsigned int minutes = (seconds % 3600) / 60;
		unsigned int sec = seconds % 60;

		if (days > 0) {
			sprintf(s, "%d:%02d:%02d:%02d", days, hours, minutes, sec);
		}
		else {
			sprintf(s, "%02d:%02d:%02d", hours, minutes, sec);
		}


		return std::string(s);
	}

	std::string formatDecimal(float v)
	{
		return std::to_string(v);
	}



	std::string GetTimeStr(double dTime) {

		char tmp[256];

		double nbDay = dTime / 86400.0;
		if (nbDay >= 1) {

			double nbYear = nbDay / 365.0;
			if (nbYear > 1) {
				if (nbYear < 5)
					sprintf(tmp, "%.1fy", nbYear);
				else
					sprintf(tmp, "%gy", nbYear);
			}
			else {
				sprintf(tmp, "%.1fd", nbDay);
			}

		}
		else {

			int iTime = (int)dTime;
			int nbHour = (int)((iTime % 86400) / 3600);
			int nbMin = (int)(((iTime % 86400) % 3600) / 60);
			int nbSec = (int)(iTime % 60);

			if (nbHour == 0) {
				if (nbMin == 0) {
					sprintf(tmp, "%02ds", nbSec);
				}
				else {
					sprintf(tmp, "%02d:%02d", nbMin, nbSec);
				}
			}
			else {
				sprintf(tmp, "%02d:%02d:%02d", nbHour, nbMin, nbSec);
			}

		}

		return std::string(tmp);

	}

	long getFileSize(const std::string& fileName)
	{
		FILE* fp = fopen(fileName.c_str(), "rb");
		if (fp == NULL) {
			return -1;
		}

		fseek(fp, 0, SEEK_END);

		long pos = ftell(fp);

		fclose(fp);

		return pos;
	}

	bool readLinesFromStream(const std::string& fileName, std::vector<std::string>& lines)
	{
		std::ifstream inFile(fileName.c_str());

		if (!inFile.is_open()) {
			return false;
		}

		return readLinesFromStream(inFile, lines);
	}

	bool readLinesFromStream(std::istream& in, std::vector<std::string>& lines)
	{
		std::string line;

		while (std::getline(in, line)) {
			if (line.length() > 0) {
				lines.push_back(line);
			}
		}

		return true;
	}

	bool appendToFile(const std::string& fileName, const std::string& s)
	{
		std::ofstream outFile;
		bool newline = false;

		if (getFileSize(fileName) > 0) {
			newline = true;
		}

		outFile.open(fileName.c_str(), std::ios::app);

		if (!outFile.is_open()) {
			return false;
		}

		// Add newline following previous line
		if (newline) {
			outFile << std::endl;
		}

		outFile << s;

		return true;
	}

	std::string format(const char* formatStr, double value)
	{
		char buf[100] = { 0 };

		sprintf(buf, formatStr, value);

		return std::string(buf);
	}

	std::string format(uint32_t value)
	{
		char buf[100] = { 0 };

		sprintf(buf, "%u", value);

		return std::string(buf);
	}

	std::string format(uint64_t value)
	{
		char buf[100] = { 0 };

		sprintf(buf, "%lld", (uint64_t)value);

		return std::string(buf);
	}

	std::string format(int value)
	{
		char buf[100] = { 0 };

		sprintf(buf, "%d", value);

		return std::string(buf);
	}

	void removeNewline(std::string& s)
	{
		size_t len = s.length();

		int toRemove = 0;

		if (len >= 2) {
			if (s[len - 2] == '\r' || s[len - 2] == '\n') {
				toRemove++;
			}
		}
		if (len >= 1) {
			if (s[len - 1] == '\r' || s[len - 1] == '\n') {
				toRemove++;
			}
		}

		if (toRemove) {
			s.erase(len - toRemove);
		}
	}

	unsigned int endian(unsigned int x)
	{
		return (x << 24) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00) | (x >> 24);
	}

	unsigned int reverse(unsigned int x)
	{
		unsigned int revX = 0;
		while (x > 0) {
			revX = revX * 10 + x % 10;
			x = x / 10;
		}
		return revX;
	}

	std::string toLower(const std::string& s)
	{
		std::string lowerCase = s;
		std::transform(lowerCase.begin(), lowerCase.end(), lowerCase.begin(), ::tolower);

		return lowerCase;
	}

	std::string trim(const std::string& s, char c)
	{
		size_t left = s.find_first_not_of(c);
		size_t right = s.find_last_not_of(c);

		return s.substr(left, right - left + 1);
	}

	std::string occurrencesOf(const std::string& s, char c)
	{
		std::stringstream ss;

		for (int i = 0; i < s.size();i++) {
			if (s[i] == c) ss << c;
		}

		return ss.str();
	}


	std::vector<secp256k1::StrideMapEntry> parseStrideFile(std::string stridesFile)
	{
		std::ifstream inFile(stridesFile.c_str());

		if (!inFile.is_open()) {
			Logger::log(LogLevel::Error, "Unable to open '" + stridesFile + "'");
			//throw KeySearchException();
		}
		bool showResults = false;
		std::vector<secp256k1::StrideMapEntry> results;
		std::vector<secp256k1::StrideMapEntry> working;

		std::string line;
		//Logger::log(LogLevel::Info, "Loading strides from '" + stridesFile + "'");
		while (std::getline(inFile, line)) {
			util::removeNewline(line);
			line = util::trim(line);

			if (line.length() > 0) {

				//Logger::log(LogLevel::Debug, "Line: " + line);
				std::vector<std::string> lineParts = util::split(line, ';');
				std::string numVal = lineParts[0];
				std::string strideVal = lineParts[1];
				std::string directiveVal = lineParts[2];

				int iVal = std::stoi(numVal);
				secp256k1::uint256 stride = 0;
				secp256k1::StrideMapEntry entry;

				try {
					stride = secp256k1::uint256(strideVal);
				}
				catch (std::string e) {
					entry.strideStr = strideVal;
				}

				entry.cycles = iVal;
				entry.stride = stride;
				entry.directive = directiveVal;

				working.push_back(entry);
			}
		}

		int repeatTarget = 0;
		int repeatCount = 0;
		int repeatIndex = 0;
		for (int i = 0; i < working.size(); i++) {

			secp256k1::StrideMapEntry sme = working[i];
			if (sme.directive == "repeat" && repeatTarget == 0) {  //set the repeaters
				repeatTarget = sme.cycles;
				repeatCount = 0;
				repeatIndex = i;
			}
			else if (sme.directive == "endrepeat") {
				//Logger::log(LogLevel::Debug, "Repeat end()");
				if ((repeatCount + 1) == repeatTarget) {
					repeatTarget = 0;
					repeatCount = 0;
					repeatIndex = 0;
				}
				else {
					repeatCount++;
					i = repeatIndex;  //trailer park goto for beginning for repeat cycle.
				}
			}
			else if (sme.directive == "reset") {
				results.push_back(sme);
			}
			else if (sme.directive == "restore") {
				if (sme.stride==0) sme.stride = 1;
				results.push_back(sme);
			}
			else if (sme.directive == "regenerate") {
				results.push_back(sme);
			}
			else if (sme.directive == "redistribute") {
				if (sme.cycles == 0) sme.cycles = 1;
				if (sme.stride == 0) sme.stride = 1;
				sme.cycles = sme.cycles * (repeatCount+1);  //increment the cycle, which in this case is the divider
				results.push_back(sme);
			}
			else if (sme.directive == "redistribute-random") {
				if (sme.cycles == 0) sme.cycles = 1;
				if (sme.stride == 0) sme.stride = 1;
				sme.cycles = sme.cycles * (repeatCount + 1);  //increment the cycle, which in this case is the divider
				results.push_back(sme);
			}
			else if (sme.directive == "redistribute-distance") {
				if (sme.cycles == 0) sme.cycles = 1;
				if (sme.stride == 0) sme.stride = 1;
				sme.cycles = sme.cycles * (repeatCount + 1);  //increment the cycle, which in this case is the divider
				results.push_back(sme);
			}
			else if (sme.directive == "stride") {
				results.push_back(sme);
			}
			else if (sme.directive.substr(0, 12) == "randomstride") {
				//Logger::log(LogLevel::Debug, "Random Stride Parsing..");
				//showResults = true;

				int minVal = 0;
				int maxVal = 3;

				std::vector<uint64_t> strideVals;

				//Random Stride - parse minmax -- assumes: randomstride:min|max;  //TODO:  less hard parse, more dummy proof
				std::vector<std::string> directiveParts = util::split(sme.directive, ':');
				if (directiveParts.size() > 0) {
					if (directiveParts[1].find('-') != std::string::npos) {
						std::vector<std::string> directiveValues = util::split(directiveParts[1], '-');
						if (directiveValues.size() == 2) {
							minVal = std::stoi(directiveValues[0]);
							maxVal = std::stoi(directiveValues[1]);
							for (int d = minVal; d <= maxVal; d++) {
								strideVals.push_back(d);
							}
						}
					}
					else if (directiveParts[1].find('|') != std::string::npos) {
						std::vector<std::string> directiveValues = util::split(directiveParts[1], '|');
						for (int d = 0; d < directiveValues.size(); d++) {
							strideVals.push_back(std::stoi(directiveValues[d]));
						}
					}
				}

				std::string strideVal = sme.strideStr;

				uint64_t timeSeed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				timeSeed = timeSeed * (i + 1);
				//std::vector<uint64_t> rnds = RandomHelper::getRndRange(timeSeed, strideVals, util::occurrencesOf(strideVal, 'x').size());
				std::vector<uint64_t> rnds = RandomHelper::getCPURndRange(timeSeed, strideVals, util::occurrencesOf(strideVal, 'x').size());

				/*
				Logger::log(LogLevel::Debug, "Random Stride Mask:" + strideVal);

				for (uint64_t rnd : rnds) {
					Logger::log(LogLevel::Debug, "RND: " + std::to_string(rnd));
				}*/

				//strideVal = "";
				sme.strideStr = "";
				sme.directive = "stride";


				std::stringstream ss;
				int idx = 0;

				//replace the mask with values
				std::size_t found = strideVal.find_first_of("x");
				while (found != std::string::npos) {
					std::string val = std::to_string(rnds[idx]);
					strideVal[found] = val[0];
					idx++;
					found = strideVal.find_first_of("x");
				}
				//Logger::log(LogLevel::Debug, "Random Stride xForm:" + strideVal);
				sme.stride = secp256k1::uint256(strideVal);

				if (sme.stride > 0) results.push_back(sme);
			}
		}

		//set (effective) indexes for later display
		int lastValue = 0;
		for (int i = 0; i < results.size();i++) {
			if (results[i].directive == "stride") {
				lastValue++;
				results[i].strideQueueIndex = lastValue;
			}
			else {
				results[i].strideQueueIndex = 0;
			}
		}

		Logger::log(LogLevel::Info, util::formatThousands(results.size()) + " stride directives parsed into " + util::formatThousands(lastValue) + " strides.");

		//set max indexes for later display (the s.q. entry may get passed around outside of the queue)
		for (int i = 0; i < results.size();i++) {
			results[i].strideQueueMax = lastValue;
		}

		return results;
	}




}