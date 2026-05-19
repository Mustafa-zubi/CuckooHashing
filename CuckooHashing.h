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
using std::mt19937;
using std::random_device;

class CuckooHashing {
private:
    static constexpr uint32_t EMPTY_SLOT = 0;
    
    static constexpr std::size_t MULTIPLIERS_PER_HASH = 3;

    vector<uint32_t> T1, T2;
    int maxLoop;
    int tableSize;
    int tableBits;
    random_device randomDevice;
    mt19937 randomNumGen;
    int elementCount;
    int insertAttempts;
    int successfulInsertions;
    int duplicateInsertions;
    int rehashCount;
    int grewOnForcedRehash;
    int failedInsertions;
    long long insertionsSinceRehash;
    long long displacementCount;
    long long lastInsertCellAccesses;
    long long insertCellAccessSum;
    long long sampledInsertCount;

    array<uint64_t, MULTIPLIERS_PER_HASH> hash1Multipliers;
    array<uint64_t, MULTIPLIERS_PER_HASH> hash2Multipliers;

    static int computeTableBits(int size);
    static int nextPowerOfTwo(int value);
    static uint32_t normalizeKey(int key);
    uint64_t randomOddMultiplier();
    uint32_t multiplicativeHash(
        uint32_t key,
        const array<uint64_t, MULTIPLIERS_PER_HASH>& multipliers) const;
    void initializeHashParameters();
    bool placeKeyInLoop(uint32_t key);
    bool rebuildTablesWithKeys(const vector<uint32_t>& keys);
    bool forcedRehashWithPendingKey(uint32_t pendingKey);
    void resizeTables(int newTableSize);
    vector<uint32_t> collectKeys() const;
    int countOccupiedSlots(const vector<uint32_t>& table) const;
    int hash1Of(uint32_t key) const;
    int hash2Of(uint32_t key) const;

public:
    CuckooHashing(int tableSize, int maxLoop);
    void insert(int key);
    void remove(int key);
    bool search(int key);
    void print(string table, int index);
    void printTables();
    int hashFunc1(int key);
    int hashFunc2(int key);
    int getTableSize() const;
    int getTableBits() const;
    int getMaxLoop() const;
    int getElementCount() const;
    int getOccupiedCountT1() const;
    int getOccupiedCountT2() const;
    int getInsertAttempts() const;
    int getSuccessfulInsertions() const;
    int getDuplicateInsertions() const;
    int getRehashCount() const;
    int getGrewOnForcedRehashCount() const;
    int getFailedInsertions() const;
    long long getInsertionsSinceRehash() const;
    long long getDisplacementCount() const;
    long long getLastInsertCellAccesses() const;
    double getAverageInsertCellAccesses() const;
    void resetInsertCellAccessStats();
    array<uint64_t, MULTIPLIERS_PER_HASH> getHash1Multipliers() const;
    array<uint64_t, MULTIPLIERS_PER_HASH> getHash2Multipliers() const;
    double getLoadFactor() const;
    void printSummary() const;
    ~CuckooHashing();
};
#endif
