#include "mathstuff.h"

#include <iostream>
#include <algorithm>
#include <numeric>
#include <list>
#include <set>
#include <iterator>
#include <string>
#include <sstream>
#include "Logger/Logger.h"
#include "Util/util.h"

template<typename InputIt, typename T>
bool nextPermutationWithRepetition(InputIt begin, InputIt end, T from_value, T to_value) {
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


template <typename T>
void Permutation(std::vector<T> v)
{
    std::sort(v.begin(), v.end());
    do {
        std::copy(v.begin(), v.end(), std::ostream_iterator<T>(std::cout, " "));
        std::cout << std::endl;
    } while (std::next_permutation(v.begin(), v.end()));
}

template <typename T>
void Combination(const std::vector<T>& v, std::size_t count)
{
    //assert(count <= v.size());
    std::vector<bool> bitset(v.size() - count, 0);
    bitset.resize(v.size(), 1);

    do {
        for (std::size_t i = 0; i != v.size(); ++i) {
            if (bitset[i]) {
                std::cout << v[i] << " ";
            }
        }
        std::cout << std::endl;
    } while (std::next_permutation(bitset.begin(), bitset.end()));
}

template <typename T>
std::vector<secp256k1::uint256> hexCombinations(const std::vector<T>& v, std::size_t count)
{
    //assert(count <= v.size());
    std::vector<secp256k1::uint256> hexResults;

    std::vector<bool> bitset(v.size() - count, 0);
    bitset.resize(v.size(), 1);

    int64_t ct = 0;
    do {
        ct++;
        std::string tmpHexString = "";
        for (std::size_t i = 0; i != v.size(); ++i) {
            if (bitset[i]) {
                tmpHexString = tmpHexString + secp256k1::uint256(v[i]).toString();
            }
        }
        if (ct % 1024==0) Logger::log(LogLevel::Info, "Hex Permutation: (" + util::formatThousands(ct) + ") " + tmpHexString);
        hexResults.push_back(tmpHexString);
    } while (std::next_permutation(bitset.begin(), bitset.end()));

    return hexResults;
}

bool increase(std::vector<bool>& bs)
{
    for (std::size_t i = 0; i != bs.size(); ++i) {
        bs[i] = !bs[i];
        if (bs[i] == true) {
            return true;
        }
    }
    return false; // overflow
}

template <typename T>
void PowerSet(const std::vector<T>& v)
{
    std::vector<bool> bitset(v.size());

    do {
        for (std::size_t i = 0; i != v.size(); ++i) {
            if (bitset[i]) {
                std::cout << v[i] << " ";
            }
        }
        std::cout << std::endl;
    } while (increase(bitset));
}


 std::vector<std::string> mathstuff::permuteOnOffString(int sz)
 {
     std::vector <std::string> results;
     std::list<int> vec(sz, 0);

     do {
         std::stringstream ss;
         std::copy(vec.begin(), vec.end(), std::ostream_iterator<int>(ss, ""));
         std::string tmp = ss.str();
         //Logger::log(LogLevel::Info, "Stride Permutation: " + tmp);
         results.push_back(tmp);
     } while (nextPermutationWithRepetition(vec.begin(), vec.end(), 0, 1));

     return results;
 }

 std::vector<std::string> mathstuff::permuteCustomRange(uint64_t sz, uint64_t min, uint64_t max)
 {
     std::vector <std::string> results;
     std::list<int> vec(sz, 0);

     do {
         std::stringstream ss;
         std::copy(vec.begin(), vec.end(), std::ostream_iterator<int>(ss, ""));
         std::string tmp = ss.str();
         //Logger::log(LogLevel::Info, "Stride Permutation: " + tmp);
         results.push_back(tmp);
     } while (nextPermutationWithRepetition(vec.begin(), vec.end(), min, max));

     return results;
 }

 std::vector<secp256k1::uint256> mathstuff::basicPermuteHexStrings(uint64_t length, uint64_t width)
 {
     std::vector <secp256k1::uint256> hexResults;
     //int hexSet[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };


     uint64_t k = length;
     std::vector<uint64_t> d;
     //build vector based on width eg, {0,1,2,3,4,....
     for (int i = 0; i < width; i++) {
         d.push_back(i);
     }

     hexResults = hexCombinations(d, length);
     //Combination(d, length);
     return hexResults;
 }

 std::vector<secp256k1::uint256> mathstuff::basicPermuteHexStrings2(uint64_t length, uint64_t width)
 {
     std::vector <secp256k1::uint256> hexResults;
     //int hexSet[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

     uint64_t k = length;
     std::vector<uint64_t> d;

     for (int i = 0; i < width; i++) {
         d.push_back(i);
     }

     std::vector <std::string> tmpResults;
     std::iota(d.begin(), d.end(), 1);
     //cout << "These are the Possible Permutations: " << endl;
     do
     {
         std::string tmpHexString = "";
         for (int i = 0; i < k; i++)
         {
            tmpHexString = tmpHexString +  std::to_string(d[i]) + " ";
         }
         tmpResults.push_back(tmpHexString);
         std::reverse(d.begin() + k, d.end());
     } while (next_permutation(d.begin(), d.end()));

     for (std::string s : tmpResults) {
         std::stringstream ss(s);
         std::istream_iterator<std::string> begin(ss);
         std::istream_iterator<std::string> end;
         std::vector<std::string> preHexStrings(begin, end);
         std::string hexString = "";
         for (std::string phs : preHexStrings) {
             int iHex = std::stoi(phs);
             hexString += secp256k1::uint256(iHex).toString();
         }
         //Logger::log(LogLevel::Info, "s: " + s + " h:" + hexString);
         hexResults.push_back(secp256k1::uint256(hexString));
     }

     return hexResults;

 }

 std::vector<secp256k1::uint256> mathstuff::permuteHexStrings(uint64_t sz, bool excludeDupes)
 {
     std::vector <secp256k1::uint256> hexResults;
     std::vector <std::string> tmpResults;
     std::list<int> vec(sz, 0);


         do {
         std::stringstream ss;
         std::copy(vec.begin(), vec.end(), std::ostream_iterator<int>(ss, " "));
         std::string tmp = ss.str();
         //Logger::log(LogLevel::Info, "Stride Permutation: " + tmp);
         tmpResults.push_back(tmp);
        } while (nextPermutationWithRepetition(vec.begin(), vec.end(), 0, 15));

     for (std::string s : tmpResults) {
         std::stringstream ss(s);
         std::istream_iterator<std::string> begin(ss);
         std::istream_iterator<std::string> end;
         std::vector<std::string> preHexStrings(begin, end);
         int subCycles = 0;
         std::string hexString = "";
         std::set<int> deduplicator;
         for (std::string phs : preHexStrings) {
             int iHex = std::stoi(phs);
             deduplicator.insert(iHex);
             subCycles++;
             hexString += secp256k1::uint256(iHex).toString();
         }
         //Logger::log(LogLevel::Info, "s: " + s + " h:" + hexString);
         if (!excludeDupes || (excludeDupes && (deduplicator.size() < subCycles)))  hexResults.push_back(secp256k1::uint256(hexString));
     }
     return hexResults;
 }


 std::vector<secp256k1::uint256> mathstuff::getStrides(secp256k1::uint256 keyMask)
 {
     std::vector<secp256k1::uint256> results;
     std::string maskString = keyMask.toString(16);
     int64_t counter = 0;
     int sz = maskString.size();
     int lastNonZero = maskString.find_last_of("1");

     Logger::log(LogLevel::Info, "Stride Mask Size: " +  util::formatThousands(sz));
     //Logger::log(LogLevel::Info, "Adjusted Stride Mask Size: " + util::formatThousands(lastNonZero+1));

     //std::vector<std::string> vectorStrings = permuteOnOffString(lastNonZero+1);
     //std::vector<std::string> vectorStrings = permuteCustomRange(lastNonZero + 1,2,3);
     std::vector<std::string> vectorStrings = {
                                            "80808080",
                                             "08080808",
                                             "00800080",
                                             "00088000",
                                             "00080008" };

     //Logger::log(LogLevel::Info, "Stride Mask: " + maskString);
     //Logger::log(LogLevel::Info, "String Stride Vector Size: " + util::formatThousands(vectorStrings.size()));
     for (std::string vs : vectorStrings) {
         //pad trailing zeros
         std::string tmp = vs;
         for (int i = lastNonZero;i < sz -1;i++) {
             tmp = tmp + "0";
         }

         results.push_back(secp256k1::uint256(tmp));
         /*
         //cull the herd
         secp256k1::uint256 k = secp256k1::uint256(tmp);
         //todo make level-of-chaos provisioning configurable
         if (k.toString().size() == (keyMask.toString().size())){
         //if (k > 0) {
             results.push_back(secp256k1::uint256(tmp));
             //if (counter % 16 ==0) Logger::log(LogLevel::Info, "Stride Queue SAMPLE: " + tmp);
         }
         */
         counter++;
     }
     Logger::log(LogLevel::Info, "Stride Queue Vector Size: " + util::formatThousands(results.size()));
     return results;
 }


 std::vector<secp256k1::uint256> mathstuff::getBasicHexPermutations(uint64_t size) {
     std::vector<secp256k1::uint256> hexResults = basicPermuteHexStrings(size,16);


     //Logger::log(LogLevel::Info, "Middle Hex String Vector Size: " + util::formatThousands(hexResults.size()));
     return hexResults;
 }

 std::vector<secp256k1::uint256> mathstuff::getHexPermutations(uint64_t size, bool excludeDupes) {
     std::vector<secp256k1::uint256> hexResults = permuteHexStrings(size, excludeDupes);


     //Logger::log(LogLevel::Info, "Middle Hex String Vector Size: " + util::formatThousands(hexResults.size()));
     return hexResults;
 }
