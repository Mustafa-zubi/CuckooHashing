#include "CuckooHashing.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using std::cout;
using std::endl;
using std::fixed;
using std::setprecision;
using std::string;
using std::swap;
using std::vector;

constexpr uint32_t CuckooHashing::EMPTY_SLOT;
constexpr std::size_t CuckooHashing::MULTIPLIERS_PER_HASH;

CuckooHashing::CuckooHashing(int requestedTableSize, int maxLoop)
    : maxLoop(maxLoop),
      tableSize(nextPowerOfTwo(requestedTableSize)),
      tableBits(computeTableBits(nextPowerOfTwo(requestedTableSize))),
      randomNumGen(randomDevice()),
      elementCount(0),
      insertAttempts(0),
      successfulInsertions(0),
      duplicateInsertions(0),
      rehashCount(0),
      grewOnForcedRehash(0),
      failedInsertions(0),
      insertionsSinceRehash(0),
      displacementCount(0),
      lastInsertCellAccesses(0),
      insertCellAccessSum(0),
      sampledInsertCount(0) {
    if (requestedTableSize <= 0) {
        throw std::invalid_argument("tableSize must be positive");
    }
    if (maxLoop <= 0) {
        throw std::invalid_argument("maxLoop must be positive");
    }

    T1.assign(tableSize, EMPTY_SLOT);
    T2.assign(tableSize, EMPTY_SLOT);
    initializeHashParameters();
}

CuckooHashing::~CuckooHashing() = default;

int CuckooHashing::computeTableBits(int size) {
    int bits = 0;
    int value = size;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

int CuckooHashing::nextPowerOfTwo(int value) {
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

uint32_t CuckooHashing::normalizeKey(int key) {
    if (key <= 0) {
        throw std::invalid_argument("CuckooHashing uses positive 32-bit signed keys; 0 is reserved as empty");
    }
    return static_cast<uint32_t>(key);
}

uint64_t CuckooHashing::randomOddMultiplier() {
    std::uniform_int_distribution<uint64_t> distribution(
        1, std::numeric_limits<uint64_t>::max());
    return distribution(randomNumGen) | 1ULL;
}

uint32_t CuckooHashing::multiplicativeHash(
    uint32_t key,
    const array<uint64_t, MULTIPLIERS_PER_HASH>& multipliers) const {
    if (tableBits == 0) {
        return 0;
    }

    
    const unsigned shift = 64u - static_cast<unsigned>(tableBits);
    const uint64_t k = static_cast<uint64_t>(key);
    uint64_t accumulator = 0;
    for (std::size_t i = 0; i < MULTIPLIERS_PER_HASH; ++i) {
        accumulator ^= (multipliers[i] * k) >> shift;
    }
    return static_cast<uint32_t>(accumulator & ((1ULL << tableBits) - 1ULL));
}

void CuckooHashing::initializeHashParameters() {
    for (std::size_t i = 0; i < MULTIPLIERS_PER_HASH; ++i) {
        hash1Multipliers[i] = randomOddMultiplier();
        hash2Multipliers[i] = randomOddMultiplier();
    }
}

int CuckooHashing::hash1Of(uint32_t key) const {
    return static_cast<int>(multiplicativeHash(key, hash1Multipliers));
}

int CuckooHashing::hash2Of(uint32_t key) const {
    return static_cast<int>(multiplicativeHash(key, hash2Multipliers));
}

int CuckooHashing::hashFunc1(int key) {
    return hash1Of(normalizeKey(key));
}

int CuckooHashing::hashFunc2(int key) {
    return hash2Of(normalizeKey(key));
}

void CuckooHashing::print(string table, int index) {
    if (index < 0 || index >= tableSize) {
        cout << "Index out of range" << endl;
        return;
    }

    if (table == "T1") {
        cout << "Table 1: " << T1[index] << endl;
    } else if (table == "T2") {
        cout << "Table 2: " << T2[index] << endl;
    } else {
        cout << "Invalid table name" << endl;
    }
}

void CuckooHashing::printTables() {
    cout << "T1:";
    for (uint32_t value : T1) {
        cout << ' ' << value;
    }
    cout << endl;

    cout << "T2:";
    for (uint32_t value : T2) {
        cout << ' ' << value;
    }
    cout << endl;
}

bool CuckooHashing::search(int key) {
    if (key <= 0) {
        return false;
    }
    const uint32_t k = static_cast<uint32_t>(key);
    return T1[hash1Of(k)] == k || T2[hash2Of(k)] == k;
}

bool CuckooHashing::placeKeyInLoop(uint32_t key) {
    
    uint32_t current = key;
    bool placeInT1 = true;

    for (int step = 0; step < maxLoop; ++step) {
        
        if (step > 0) {
            ++lastInsertCellAccesses;
        }
        if (placeInT1) {
            const int idx = hash1Of(current);
            if (T1[idx] == EMPTY_SLOT) {
                T1[idx] = current;
                return true;
            }
            ++displacementCount;
            swap(current, T1[idx]);
        } else {
            const int idx = hash2Of(current);
            if (T2[idx] == EMPTY_SLOT) {
                T2[idx] = current;
                return true;
            }
            ++displacementCount;
            swap(current, T2[idx]);
        }
        placeInT1 = !placeInT1;
    }

    return false;
}

void CuckooHashing::insert(int key) {
    ++insertAttempts;
    const uint32_t k = normalizeKey(key);

    lastInsertCellAccesses = 0;
    const int i1 = hash1Of(k);
    ++lastInsertCellAccesses;
    if (T1[i1] == k) {
        ++duplicateInsertions;
        insertCellAccessSum += lastInsertCellAccesses;
        ++sampledInsertCount;
        return;
    }
    const int i2 = hash2Of(k);
    ++lastInsertCellAccesses;
    if (T2[i2] == k) {
        ++duplicateInsertions;
        insertCellAccessSum += lastInsertCellAccesses;
        ++sampledInsertCount;
        return;
    }


    const long long r = static_cast<long long>(tableSize);
    if (insertionsSinceRehash >= r * r) {
        const vector<uint32_t> keys = collectKeys();
        if (!rebuildTablesWithKeys(keys)) {

            resizeTables(tableSize * 2);
            const vector<uint32_t> grown = collectKeys();
            if (!rebuildTablesWithKeys(grown)) {
                throw std::runtime_error("CuckooHashing: r^2 refresh failed after grow");
            }
        }
    }

    if (placeKeyInLoop(k)) {
        ++elementCount;
        ++successfulInsertions;
        ++insertionsSinceRehash;
        insertCellAccessSum += lastInsertCellAccesses;
        ++sampledInsertCount;
        return;
    }

    if (!forcedRehashWithPendingKey(k)) {
        ++failedInsertions;
        throw std::runtime_error("CuckooHashing: insertion failed after forced rehash/grow");
    }

    ++elementCount;
    ++successfulInsertions;
    ++insertionsSinceRehash;

}

long long CuckooHashing::getLastInsertCellAccesses() const {
    return lastInsertCellAccesses;
}

double CuckooHashing::getAverageInsertCellAccesses() const {
    return sampledInsertCount == 0
               ? 0.0
               : static_cast<double>(insertCellAccessSum) /
                     static_cast<double>(sampledInsertCount);
}

void CuckooHashing::resetInsertCellAccessStats() {
    lastInsertCellAccesses = 0;
    insertCellAccessSum = 0;
    sampledInsertCount = 0;
}

void CuckooHashing::remove(int key) {
    if (key <= 0) {
        return;
    }
    const uint32_t k = static_cast<uint32_t>(key);

    const int i1 = hash1Of(k);
    if (T1[i1] == k) {
        T1[i1] = EMPTY_SLOT;
        --elementCount;
        return;
    }

    const int i2 = hash2Of(k);
    if (T2[i2] == k) {
        T2[i2] = EMPTY_SLOT;
        --elementCount;
    }
}

vector<uint32_t> CuckooHashing::collectKeys() const {
    vector<uint32_t> keys;
    keys.reserve(static_cast<std::size_t>(elementCount));
    for (uint32_t v : T1) {
        if (v != EMPTY_SLOT) keys.push_back(v);
    }
    for (uint32_t v : T2) {
        if (v != EMPTY_SLOT) keys.push_back(v);
    }
    return keys;
}

void CuckooHashing::resizeTables(int newTableSize) {
    tableSize = newTableSize;
    tableBits = computeTableBits(newTableSize);
    T1.assign(tableSize, EMPTY_SLOT);
    T2.assign(tableSize, EMPTY_SLOT);
    elementCount = 0;
}

bool CuckooHashing::rebuildTablesWithKeys(const vector<uint32_t>& keys) {

    for (int attempt = 0; attempt < maxLoop; ++attempt) {
        std::fill(T1.begin(), T1.end(), EMPTY_SLOT);
        std::fill(T2.begin(), T2.end(), EMPTY_SLOT);
        elementCount = 0;
        ++rehashCount;
        initializeHashParameters();

        bool allPlaced = true;
        for (uint32_t k : keys) {
            if (!placeKeyInLoop(k)) {
                allPlaced = false;
                break;
            }
            ++elementCount;
        }

        if (allPlaced) {
            insertionsSinceRehash = 0;
            return true;
        }
    }
    return false;
}

bool CuckooHashing::forcedRehashWithPendingKey(uint32_t pendingKey) {
    vector<uint32_t> keys = collectKeys();
    keys.push_back(pendingKey);

    const double loadAfterInsert =
        static_cast<double>(keys.size()) / (2.0 * static_cast<double>(tableSize));

    if (loadAfterInsert > 5.0 / 12.0) {
        resizeTables(tableSize * 2);
        ++grewOnForcedRehash;
    }

    if (rebuildTablesWithKeys(keys)) {
        return true;
    }

    while (true) {
        resizeTables(tableSize * 2);
        ++grewOnForcedRehash;
        if (rebuildTablesWithKeys(keys)) {
            return true;
        }
        if (tableSize >= (1 << 30)) {
            return false;
        }
    }
}

int CuckooHashing::countOccupiedSlots(const vector<uint32_t>& table) const {
    return static_cast<int>(std::count_if(table.begin(), table.end(),
        [](uint32_t value) { return value != EMPTY_SLOT; }));
}

int CuckooHashing::getTableSize() const { return tableSize; }
int CuckooHashing::getTableBits() const { return tableBits; }
int CuckooHashing::getMaxLoop() const { return maxLoop; }
int CuckooHashing::getElementCount() const { return elementCount; }
int CuckooHashing::getOccupiedCountT1() const { return countOccupiedSlots(T1); }
int CuckooHashing::getOccupiedCountT2() const { return countOccupiedSlots(T2); }
int CuckooHashing::getInsertAttempts() const { return insertAttempts; }
int CuckooHashing::getSuccessfulInsertions() const { return successfulInsertions; }
int CuckooHashing::getDuplicateInsertions() const { return duplicateInsertions; }
int CuckooHashing::getRehashCount() const { return rehashCount; }
int CuckooHashing::getGrewOnForcedRehashCount() const { return grewOnForcedRehash; }
int CuckooHashing::getFailedInsertions() const { return failedInsertions; }
long long CuckooHashing::getInsertionsSinceRehash() const { return insertionsSinceRehash; }
long long CuckooHashing::getDisplacementCount() const { return displacementCount; }

array<uint64_t, CuckooHashing::MULTIPLIERS_PER_HASH> CuckooHashing::getHash1Multipliers() const {
    return hash1Multipliers;
}

array<uint64_t, CuckooHashing::MULTIPLIERS_PER_HASH> CuckooHashing::getHash2Multipliers() const {
    return hash2Multipliers;
}

double CuckooHashing::getLoadFactor() const {
    const double totalSlots = 2.0 * static_cast<double>(tableSize);
    return totalSlots == 0.0 ? 0.0 : static_cast<double>(elementCount) / totalSlots;
}

void CuckooHashing::printSummary() const {
    cout << "\n=== Cuckoo Hashing Summary ===" << endl;
    cout << "Parameters:" << endl;
    cout << "  Per-table size r: " << tableSize << " (q=" << tableBits << " bits)" << endl;
    cout << "  Total slots (2r): " << (2 * tableSize) << endl;
    cout << "  MaxLoop: " << maxLoop << endl;
    cout << "  Hash family: XOR of " << MULTIPLIERS_PER_HASH
         << " Dietzfelbinger multiply-shift functions per table" << endl;
    cout << "  Forced refresh threshold r^2: "
         << (static_cast<long long>(tableSize) * tableSize) << " insertions" << endl;

    cout << "Runtime statistics:" << endl;
    cout << "  Active keys: " << elementCount << endl;
    cout << "  Occupied slots in T1: " << getOccupiedCountT1() << endl;
    cout << "  Occupied slots in T2: " << getOccupiedCountT2() << endl;
    cout << "  Load factor n/(2r): " << fixed << setprecision(4) << getLoadFactor() << endl;
    cout << "  Insert attempts: " << insertAttempts << endl;
    cout << "  Successful insertions: " << successfulInsertions << endl;
    cout << "  Duplicate insertions ignored: " << duplicateInsertions << endl;
    cout << "  Failed insertions: " << failedInsertions << endl;
    cout << "  Rehash attempts: " << rehashCount << endl;
    cout << "  Grew on forced rehash (>5/12): " << grewOnForcedRehash << endl;
    cout << "  Insertions since last rehash: " << insertionsSinceRehash << endl;
    cout << "  Key displacements: " << displacementCount << endl;
}
