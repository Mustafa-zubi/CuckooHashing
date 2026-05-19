#ifndef ROBINHOODHASHING_H
#define ROBINHOODHASHING_H

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using std::array;
using std::mt19937;
using std::random_device;
using std::string;
using std::uint32_t;
using std::uint64_t;
using std::vector;

// Robin Hood hashing (Celis 1986): open-addressing table where, on collision,
// the resident with the shorter probe sequence length (PSL) is swapped out
// in favor of the incoming key with the longer PSL. This minimizes the
// variance of PSLs, capping worst-case lookup distance.
//
// Lookups can early-exit: when we have walked `dist` slots from the ideal
// position and the resident at the current slot has PSL < dist, our key
// would have stolen this slot — therefore it cannot exist in the table.
//
// Deletions back-shift subsequent entries to preserve the invariant that
// no resident has PSL > 0 with an empty slot earlier in its probe path.
class RobinHoodHashing {
private:
    vector<uint32_t> table;
    int tableSize;
    int tableBits;
    random_device randomDevice;
    mt19937 randomNumGen;
    int elementCount;
    int insertAttempts;
    int successfulInsertions;
    int duplicateInsertions;
    int failedInsertions;
    long long probeCount;
    long long swapCount;
    int maxProbeDistance;
    static constexpr uint32_t EMPTY_SLOT = 0;
    static constexpr std::size_t UNIVERSAL_HASH_COEFFICIENTS = 1;

    array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> hashCoefficients;

    int computeTableBits(int size) const;
    int nextPowerOfTwo(int value) const;
    uint32_t normalizeKey(int key) const;
    uint64_t randomCoefficient();
    uint32_t multiplicativeHash(uint32_t key) const;
    int idealIndexFor(uint32_t key) const;
    int probeDistanceOf(int currentIndex, uint32_t residentKey) const;
    void initializeHashParameters();
    int countOccupiedSlots() const;

    // Pagh & Rodler Fig 7 instrumentation: per-insert count of accesses to a
    // distinct cache line during placement. Like Linear Probing, consecutive
    // probes that share a 64-byte line cost zero additional cache misses.
    static constexpr int CACHE_LINE_SLOTS = 16;
    long long lastInsertCellAccesses = 0;
    long long insertCellAccessSum = 0;
    long long sampledInsertCount = 0;

public:
    explicit RobinHoodHashing(int requestedTableSize);
    void insert(int key);
    void remove(int key);
    bool search(int key);
    void print(string tableName, int index);
    void printTables();
    int hashFunc(int key);
    int getTableSize() const;
    int getTableBits() const;
    int getElementCount() const;
    int getOccupiedSlotCount() const;
    int getInsertAttempts() const;
    int getSuccessfulInsertions() const;
    int getDuplicateInsertions() const;
    int getFailedInsertions() const;
    long long getProbeCount() const;
    long long getSwapCount() const;
    int getMaxProbeDistance() const;
    array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> getHashCoefficients() const;
    double getLoadFactor() const;
    double getAverageInsertCellAccesses() const;
    void resetInsertCellAccessStats();
    void printSummary() const;
    ~RobinHoodHashing();
};

#endif
