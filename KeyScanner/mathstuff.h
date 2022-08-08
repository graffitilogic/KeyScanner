#pragma once
#include "../KeyScanner/Secp256k1/secp256k1.h"
#include <vector>

class mathstuff
{

private:

	template<typename InputIt, typename T>
	bool nextPermutationWithRepetition(InputIt begin, InputIt end, T from_value, T to_value);
	std::vector<std::string> permuteOnOffString(int sz);
    std::vector<std::string> permuteCustomRange(uint64_t sz, uint64_t min, uint64_t max);

    std::vector<secp256k1::uint256> basicPermuteHexStrings(uint64_t length, uint64_t width);
    std::vector<secp256k1::uint256> basicPermuteHexStrings2(uint64_t length, uint64_t width);


    std::vector<secp256k1::uint256> permuteHexStrings(uint64_t sz, bool excludeDupes);

public:

	std::vector<secp256k1::uint256> getStrides(secp256k1::uint256 keyMask);

    std::vector<secp256k1::uint256> getBasicHexPermutations(uint64_t size);
    std::vector<secp256k1::uint256> getHexPermutations(uint64_t size, bool excludeDupes);

};

template<typename InputIt, typename T>
inline bool mathstuff::nextPermutationWithRepetition(InputIt begin, InputIt end, T from_value, T to_value)
{
    auto it = std::find_if_not(std::make_reverse_iterator(end),
        std::make_reverse_iterator(begin),
        [&to_value](auto current) { return to_value == current; });

    if (it == std::make_reverse_iterator(begin))
        return false;

    auto bound_element_iterator = std::prev(it.base());

    (*bound_element_iterator)++;
    std::fill(std::next(bound_element_iterator), end, from_value);

    return true;
}
