#ifndef CUCKOOHASHING_H
#define CUCKOOHASHING_H
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using std::array;
using std::uint32_t;
using std::uint64_t;
using std::string;
using std::vector;
using std::mt19937; // MZ random number generator
using std::random_device; // for generating random numbers

class CuckooHashing {
private:
   /* 
   MZ2 -- hashing tables size: each should be 10% 20% larger than half number of the elements 
   as replorted in the paper
   */ 
   
   vector<uint32_t> T1, T2;
   int maxRehashLimit; // maximum number of rehashes allowed
   const int tableSize;
   const int tableBits;
   random_device randomDevice;
   mt19937 randomNumGen; 
   int elementCount;
   int insertAttempts;
   int successfulInsertions;
   int duplicateInsertions;
   int rehashCount;
   int failedInsertions;
   int insertionsSinceRehash;
   long long displacementCount;
   static constexpr uint32_t EMPTY_SLOT = 0;
   static constexpr uint64_t UNIVERSAL_HASH_PRIME = 4294967311ULL;
   static constexpr std::size_t UNIVERSAL_HASH_COEFFICIENTS = 3;

   array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> hash1Coefficients;
   array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> hash2Coefficients;

   int computeTableBits(int size) const;
   int nextPowerOfTwo(int value) const;
   uint32_t normalizeKey(int key) const;
   uint64_t randomCoefficient();
   uint32_t polynomialUniversalHash(uint32_t key, const array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS>& coefficients) const;
   void initializeHashParameters();
   bool placeKeyWithoutRehash(uint32_t key);
   bool rebuildTablesWithKeys(const vector<uint32_t>& keys);
   bool rehashWithPendingKey(uint32_t key);
   void maybeForceRefreshRehash();
   vector<uint32_t> collectKeys() const;
   int countOccupiedSlots(const vector<uint32_t>& table) const;

public:
    CuckooHashing(int tableSize, int maxRehashLimit);
    void insert(int key);
    void remove(int key);
    bool search(int key);
    void print(string table, int index);
    void printTables();
    int hashFunc1(int key);
    int hashFunc2(int key);
    int getTableSize() const;
    int getTableBits() const;
    int getMaxRehashLimit() const;
    int getElementCount() const;
    int getOccupiedCountT1() const;
    int getOccupiedCountT2() const;
    int getInsertAttempts() const;
    int getSuccessfulInsertions() const;
    int getDuplicateInsertions() const;
    int getRehashCount() const;
    int getFailedInsertions() const;
    int getInsertionsSinceRehash() const;
    long long getDisplacementCount() const;
    array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> getHash1Coefficients() const;
    array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> getHash2Coefficients() const;
    double getLoadFactor() const;
    void printSummary() const;
    ~CuckooHashing();
};
#endif
