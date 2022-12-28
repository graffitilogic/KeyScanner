#include "OpenSSLRNG.h"
#include  <secp256k1.h>

std::vector<std::string> OpenSSLRNG::getRandoms(std::string min, std::string max, uint64_t length) {

	std::vector<std::string> results;

	secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
	EC_GROUP* group = EC_GROUP_new_by_curve_name(714);  //hardcoded from openSSL ref because of collission ->NID_secp256k1);

	BIGNUM* minNum = BN_new();
	BIGNUM* maxNum = BN_new();


	BN_hex2bn(&minNum, min.c_str());
	BN_hex2bn(&maxNum, max.c_str());

	BN_CTX* bn_ctx = NULL;
	//BIGNUM* order = NULL;

	//order = BN_new();
	bn_ctx = BN_CTX_new();

	uint64_t generatorCount = 0;

	while (generatorCount < length) {
		//Capture the timestamp that will be used by the rng.
		//time_t rngTime = time(NULL);

		//get random 256b number from openSSL
		BIGNUM* bnPK = new BIGNUM();
		BN_rand_range(bnPK, maxNum);

		//bn_rand_range_test(0, bnPK, maxNum);
		//BN_rand_range(bnPK, maxNum);

		//regen until in range. maxnum is automatic, min is not
		while (BN_cmp(minNum, bnPK) > 0) {
			BN_rand_range(bnPK, maxNum);
			//bn_rand_range_test(0, bnPK, maxNum);
			//cout << "iteration..";
		}

		std::string privateKey = "";

		//std::stringstream pkBuffer;
		//pkBuffer << bnPK;

		//string pkShort = pkBuffer.str();

		//cout << "Converting BN to Hex " << bnPK << std::endl;
		privateKey = BN_bn2hex(bnPK);
		results.push_back(privateKey);
		generatorCount++;
	}

	return results;
}


