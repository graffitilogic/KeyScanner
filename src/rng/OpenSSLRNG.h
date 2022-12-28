

#include <string>
#include <vector>
//#include "IntGroup.h"
#include <chrono>

#include <openssl/buffer.h>
#include <openssl/ecdsa.h>
//#include <openssl/evp.h>
#include <openssl/rand.h>
//#include <openssl/sha.h>
//#include <openssl/ripemd.h>

#pragma once
class OpenSSLRNG
{

	public:

		static std::vector<std::string> getRandoms(std::string min, std::string max, uint64_t length);
};

