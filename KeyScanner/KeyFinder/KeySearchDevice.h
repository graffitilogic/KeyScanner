#ifndef _KEY_SEARCH_DEVICE_H
#define _KEY_SEARCH_DEVICE_H

#include <vector>
#include <set>
#include "../Secp256k1/secp256k1.h"
#include "KeySearchTypes.h"


class KeySearchException {

public:

	KeySearchException()
	{

	}

	KeySearchException(const std::string& msg)
	{
		this->msg = msg;
	}

	std::string msg;
};

typedef struct {
	int thread;
	int block;
	int idx;
	int threadId;
	int offset;
}GPUMatrix;


typedef struct {
	std::string address;
	secp256k1::ecpoint publicKey;
	secp256k1::uint256 privateKey;
	secp256k1::uint256 exponent;
	secp256k1::uint256 stride;
	int strideQueueIndex;
	GPUMatrix offsetMatrix;
	unsigned int hash[5];
	unsigned int x[8];
	bool compressed;
}KeySearchResult;


#endif