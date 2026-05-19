#include "RobinHoodHashing.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

using std::cout;
using std::endl;
using std::fixed;
using std::setprecision;
using std::swap;

constexpr uint32_t RobinHoodHashing::EMPTY_SLOT;

RobinHoodHashing::RobinHoodHashing(int requestedTableSize)
    : tableSize(nextPowerOfTwo(requestedTableSize)),
      tableBits(computeTableBits(nextPowerOfTwo(requestedTableSize))),
      randomNumGen(randomDevice()),
      elementCount(0),
      insertAttempts(0),
      successfulInsertions(0),
      duplicateInsertions(0),
      failedInsertions(0),
      probeCount(0),
      swapCount(0),
      maxProbeDistance(0) {
    if (requestedTableSize <= 0) {
        throw std::invalid_argument("tableSize must be positive");
    }

    table.assign(tableSize, EMPTY_SLOT);
    initializeHashParameters();
}

RobinHoodHashing::~RobinHoodHashing() = default;

int RobinHoodHashing::computeTableBits(int size) const {
    int bits = 0;
    int value = size;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

int RobinHoodHashing::nextPowerOfTwo(int value) const {
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

uint32_t RobinHoodHashing::normalizeKey(int key) const {
    if (key <= 0) {
        throw std::invalid_argument("RobinHoodHashing uses positive 32-bit signed keys; 0 is reserved as empty");
    }
    return static_cast<uint32_t>(key);
}

uint64_t RobinHoodHashing::randomCoefficient() {
    std::uniform_int_distribution<uint64_t> distribution(
        1, std::numeric_limits<uint64_t>::max());
    return distribution(randomNumGen) | 1ULL;
}

uint32_t RobinHoodHashing::multiplicativeHash(uint32_t key) const {
    // Dietzfelbinger multiply-shift: h_a(x) = (a*x mod 2^w) div 2^(w-q), a odd.
    if (tableBits == 0) {
        return 0;
    }
    const unsigned shift = 64u - static_cast<unsigned>(tableBits);
    return static_cast<uint32_t>(
        (hashCoefficients[0] * static_cast<uint64_t>(key)) >> shift);
}

void RobinHoodHashing::initializeHashParameters() {
    for (std::size_t i = 0; i < hashCoefficients.size(); ++i) {
        hashCoefficients[i] = randomCoefficient();
    }
}

int RobinHoodHashing::idealIndexFor(uint32_t key) const {
    return static_cast<int>(multiplicativeHash(key));
}

int RobinHoodHashing::probeDistanceOf(int currentIndex, uint32_t residentKey) const {
    const int ideal = idealIndexFor(residentKey);
    return (currentIndex - ideal + tableSize) & (tableSize - 1);
}

int RobinHoodHashing::hashFunc(int key) {
    return idealIndexFor(normalizeKey(key));
}

bool RobinHoodHashing::search(int key) {
    if (key <= 0) {
        return false;
    }
    const uint32_t k = static_cast<uint32_t>(key);
    const int ideal = idealIndexFor(k);
    const int mask = tableSize - 1;

    for (int dist = 0; dist < tableSize; ++dist) {
        const int idx = (ideal + dist) & mask;
        ++probeCount;
        const uint32_t resident = table[idx];
        if (resident == EMPTY_SLOT) {
            return false;
        }
        if (resident == k) {
            return true;
        }
        // Robin Hood early exit: if the resident has a shorter PSL than
        // ours, our key would have stolen this slot during insertion — so
        // it cannot be further along in the probe sequence.
        const int residentDist = probeDistanceOf(idx, resident);
        if (residentDist < dist) {
            return false;
        }
    }
    return false;
}

void RobinHoodHashing::insert(int key) {
    ++insertAttempts;
    const uint32_t newKey = normalizeKey(key);

    // Duplicate check first so we mirror the paper's lookup-before-insert.
    if (search(static_cast<int>(newKey))) {
        ++duplicateInsertions;
        // Counts as one cache-line touch for the search prefix.
        insertCellAccessSum += 1;
        ++sampledInsertCount;
        return;
    }

    uint32_t currentKey = newKey;
    int currentDist = 0;
    int idx = idealIndexFor(currentKey);
    const int mask = tableSize - 1;

    // Fig 7 instrumentation: count distinct cache lines touched during the
    // placement walk. Consecutive probes that share a cache line cost zero
    // additional cache misses.
    lastInsertCellAccesses = 0;
    int lastCacheLine = -1;

    for (int step = 0; step < tableSize; ++step) {
        const int cacheLine = idx / CACHE_LINE_SLOTS;
        if (cacheLine != lastCacheLine) {
            ++lastInsertCellAccesses;
            lastCacheLine = cacheLine;
        }
        ++probeCount;
        if (table[idx] == EMPTY_SLOT) {
            table[idx] = currentKey;
            ++elementCount;
            ++successfulInsertions;
            if (currentDist > maxProbeDistance) {
                maxProbeDistance = currentDist;
            }
            insertCellAccessSum += lastInsertCellAccesses;
            ++sampledInsertCount;
            return;
        }

        const int residentDist = probeDistanceOf(idx, table[idx]);
        if (residentDist < currentDist) {
            // Steal: the rich resident has a shorter PSL than we do, so
            // swap them out (Robin Hood takes from the rich, gives to the
            // poor — the new key adopts this slot).
            ++swapCount;
            swap(currentKey, table[idx]);
            if (currentDist > maxProbeDistance) {
                maxProbeDistance = currentDist;
            }
            currentDist = residentDist;
        }

        ++currentDist;
        idx = (idx + 1) & mask;
    }

    ++failedInsertions;
    throw std::runtime_error("RobinHoodHashing: insertion failed because the table is full");
}

double RobinHoodHashing::getAverageInsertCellAccesses() const {
    return sampledInsertCount == 0
               ? 0.0
               : static_cast<double>(insertCellAccessSum) /
                     static_cast<double>(sampledInsertCount);
}

void RobinHoodHashing::resetInsertCellAccessStats() {
    lastInsertCellAccesses = 0;
    insertCellAccessSum = 0;
    sampledInsertCount = 0;
}

void RobinHoodHashing::remove(int key) {
    if (key <= 0) {
        return;
    }
    const uint32_t k = static_cast<uint32_t>(key);
    const int ideal = idealIndexFor(k);
    const int mask = tableSize - 1;

    // Find the slot holding `k`.
    int idx = ideal;
    int dist = 0;
    bool found = false;
    for (int step = 0; step < tableSize; ++step) {
        ++probeCount;
        const uint32_t resident = table[idx];
        if (resident == EMPTY_SLOT) {
            return;
        }
        if (resident == k) {
            found = true;
            break;
        }
        const int residentDist = probeDistanceOf(idx, resident);
        if (residentDist < dist) {
            return;
        }
        ++dist;
        idx = (idx + 1) & mask;
    }
    if (!found) {
        return;
    }

    // Back-shift subsequent entries so no resident retains a PSL > 0 that
    // could have legally moved into the freed slot.
    table[idx] = EMPTY_SLOT;
    --elementCount;

    int nextIdx = (idx + 1) & mask;
    while (table[nextIdx] != EMPTY_SLOT) {
        const int nextResidentDist = probeDistanceOf(nextIdx, table[nextIdx]);
        if (nextResidentDist == 0) {
            break;
        }
        table[idx] = table[nextIdx];
        table[nextIdx] = EMPTY_SLOT;
        idx = nextIdx;
        nextIdx = (nextIdx + 1) & mask;
    }
}

void RobinHoodHashing::print(string tableName, int index) {
    if (index < 0 || index >= tableSize) {
        cout << "Index out of range" << endl;
        return;
    }
    if (tableName != "T" && tableName != "table") {
        cout << "Invalid table name" << endl;
        return;
    }
    cout << "Slot " << index << ": " << table[index] << endl;
}

void RobinHoodHashing::printTables() {
    cout << "Robin Hood table:";
    for (uint32_t value : table) {
        cout << ' ' << value;
    }
    cout << endl;
}

int RobinHoodHashing::countOccupiedSlots() const {
    return static_cast<int>(std::count_if(table.begin(), table.end(),
        [](uint32_t value) { return value != EMPTY_SLOT; }));
}

int RobinHoodHashing::getTableSize() const { return tableSize; }
int RobinHoodHashing::getTableBits() const { return tableBits; }
int RobinHoodHashing::getElementCount() const { return elementCount; }
int RobinHoodHashing::getOccupiedSlotCount() const { return countOccupiedSlots(); }
int RobinHoodHashing::getInsertAttempts() const { return insertAttempts; }
int RobinHoodHashing::getSuccessfulInsertions() const { return successfulInsertions; }
int RobinHoodHashing::getDuplicateInsertions() const { return duplicateInsertions; }
int RobinHoodHashing::getFailedInsertions() const { return failedInsertions; }
long long RobinHoodHashing::getProbeCount() const { return probeCount; }
long long RobinHoodHashing::getSwapCount() const { return swapCount; }
int RobinHoodHashing::getMaxProbeDistance() const { return maxProbeDistance; }

array<uint64_t, RobinHoodHashing::UNIVERSAL_HASH_COEFFICIENTS>
RobinHoodHashing::getHashCoefficients() const {
    return hashCoefficients;
}

double RobinHoodHashing::getLoadFactor() const {
    return tableSize == 0 ? 0.0
                          : static_cast<double>(elementCount) /
                                static_cast<double>(tableSize);
}

void RobinHoodHashing::printSummary() const {
    const array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> h = getHashCoefficients();

    cout << "\n=== Robin Hood Hashing Summary ===" << endl;
    cout << "Parameters:" << endl;
    cout << "  Table size (rounded to power of two): " << tableSize << endl;
    cout << "  Table index bits q: " << tableBits << endl;
    cout << "  Empty slot sentinel: " << EMPTY_SLOT << endl;
    cout << "  Hash family: Dietzfelbinger multiply-shift" << endl;
    cout << "  Hash multiplier: " << h[0] << endl;

    cout << "Runtime statistics:" << endl;
    cout << "  Active keys: " << elementCount << endl;
    cout << "  Occupied slots: " << getOccupiedSlotCount() << endl;
    cout << "  Load factor: " << fixed << setprecision(4) << getLoadFactor() << endl;
    cout << "  Insert attempts: " << insertAttempts << endl;
    cout << "  Successful insertions: " << successfulInsertions << endl;
    cout << "  Duplicate insertions ignored: " << duplicateInsertions << endl;
    cout << "  Failed insertions: " << failedInsertions << endl;
    cout << "  Robin Hood swaps during insert: " << swapCount << endl;
    cout << "  Total probes: " << probeCount << endl;
    cout << "  Maximum probe distance: " << maxProbeDistance << endl;
}
