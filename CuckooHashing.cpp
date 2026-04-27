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

CuckooHashing::CuckooHashing(int requestedTableSize, int maxRehashLimit)
    : maxRehashLimit(maxRehashLimit),
      tableSize(nextPowerOfTwo(requestedTableSize)),
      tableBits(computeTableBits(nextPowerOfTwo(requestedTableSize))),
      randomNumGen(randomDevice()),
      elementCount(0),
      insertAttempts(0),
      successfulInsertions(0),
      duplicateInsertions(0),
      rehashCount(0),
      failedInsertions(0),
      insertionsSinceRehash(0),
      displacementCount(0) {
    if (requestedTableSize <= 0) {
        throw std::invalid_argument("tableSize must be positive");
    }
    if (maxRehashLimit <= 0) {
        throw std::invalid_argument("maxRehashLimit must be positive");
    }

    T1.assign(tableSize, EMPTY_SLOT);
    T2.assign(tableSize, EMPTY_SLOT);
    initializeHashParameters();
}

CuckooHashing::~CuckooHashing() {
    std::cout << "CuckooHashing destructor called" << std::endl;
}

int CuckooHashing::computeTableBits(int size) const {
    int bits = 0;
    int value = size;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

int CuckooHashing::nextPowerOfTwo(int value) const {
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

uint32_t CuckooHashing::normalizeKey(int key) const {
    if (key <= 0) {
        throw std::invalid_argument("MZ1 -- in ref to paper, it uses positive 32-bit signed keys and reserves 0 as empty");
    }
    return static_cast<uint32_t>(key);
}

uint64_t CuckooHashing::randomCoefficient() {
    std::uniform_int_distribution<uint64_t> distribution(0, UNIVERSAL_HASH_PRIME - 1);
    return distribution(randomNumGen);
}

uint32_t CuckooHashing::polynomialUniversalHash(
    uint32_t key,
    const array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS>& coefficients) const {
    uint64_t result = 0;
    uint64_t power = 1;
    const uint64_t normalizedKey = key;

    for (uint64_t coefficient : coefficients) {
        result = (result + ((coefficient * power) % UNIVERSAL_HASH_PRIME)) % UNIVERSAL_HASH_PRIME;
        power = (power * normalizedKey) % UNIVERSAL_HASH_PRIME;
    }

    return static_cast<uint32_t>(result % static_cast<uint64_t>(tableSize));
}

void CuckooHashing::initializeHashParameters() {
    for (std::size_t i = 0; i < hash1Coefficients.size(); ++i) {
        hash1Coefficients[i] = randomCoefficient();
        hash2Coefficients[i] = randomCoefficient();
    }
}

int CuckooHashing::hashFunc1(int key) {
    const uint32_t normalizedKey = normalizeKey(key); //MZ1 
    return static_cast<int>(polynomialUniversalHash(normalizedKey, hash1Coefficients));
}
int CuckooHashing::hashFunc2(int key) {
    const uint32_t normalizedKey = normalizeKey(key);
    return static_cast<int>(polynomialUniversalHash(normalizedKey, hash2Coefficients));
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
    cout << "T1: ";
    for (uint32_t value : T1) {
        cout << value << ' ';
    }
    cout << endl;

    cout << "T2: ";
    for (uint32_t value : T2) {
        cout << value << ' ';
    }
    cout << endl;
}

bool CuckooHashing::search(int key) {
    if (key <= 0) {
        return false;
    }

    const uint32_t normalizedKey = static_cast<uint32_t>(key);
    return T1[hashFunc1(key)] == normalizedKey || T2[hashFunc2(key)] == normalizedKey;
}

void CuckooHashing::insert(int key) {
    ++insertAttempts;

    const uint32_t normalizedKey = normalizeKey(key);

    if (search(key)) {
        ++duplicateInsertions;
        return;
    }

    maybeForceRefreshRehash();

    if (placeKeyWithoutRehash(normalizedKey)) {
        ++elementCount;
        ++successfulInsertions;
        ++insertionsSinceRehash;
        return;
    }

    if (!rehashWithPendingKey(normalizedKey)) {
        ++failedInsertions;
        throw std::runtime_error("Failed to insert key after rehash attempts");
    }

    ++elementCount;
    ++successfulInsertions;
    ++insertionsSinceRehash;
}

void CuckooHashing::remove(int key) {
    if (key <= 0) {
        return;
    }

    const uint32_t normalizedKey = static_cast<uint32_t>(key);

    const int index1 = hashFunc1(key);
    if (T1[index1] == normalizedKey) {
        T1[index1] = EMPTY_SLOT;
        --elementCount;
        return;
    }

    const int index2 = hashFunc2(key);
    if (T2[index2] == normalizedKey) {
        T2[index2] = EMPTY_SLOT;
        --elementCount;
    }
}

bool CuckooHashing::placeKeyWithoutRehash(uint32_t key) {
    uint32_t currentKey = key;
    bool placeInFirstTable = true;

    for (int loopCount = 0; loopCount < maxRehashLimit; ++loopCount) {
        if (placeInFirstTable) {
            const int index = hashFunc1(static_cast<int>(currentKey));
            if (T1[index] == EMPTY_SLOT) {
                T1[index] = currentKey;
                return true;
            }
            ++displacementCount;
            swap(currentKey, T1[index]);
        } else {
            const int index = hashFunc2(static_cast<int>(currentKey));
            if (T2[index] == EMPTY_SLOT) {
                T2[index] = currentKey;
                return true;
            }
            ++displacementCount;
            swap(currentKey, T2[index]);
        }

        placeInFirstTable = !placeInFirstTable;
    }

    return false;
}

vector<uint32_t> CuckooHashing::collectKeys() const {
    vector<uint32_t> keys;
    keys.reserve(static_cast<std::size_t>(elementCount));

    for (uint32_t value : T1) {
        if (value != EMPTY_SLOT) {
            keys.push_back(value);
        }
    }

    for (uint32_t value : T2) {
        if (value != EMPTY_SLOT) {
            keys.push_back(value);
        }
    }

    return keys;
}

bool CuckooHashing::rebuildTablesWithKeys(const vector<uint32_t>& keys) {
    for (int attempt = 0; attempt < maxRehashLimit; ++attempt) {
        vector<uint32_t> newT1(tableSize, EMPTY_SLOT);
        vector<uint32_t> newT2(tableSize, EMPTY_SLOT);

        ++rehashCount;
        initializeHashParameters();
        T1.swap(newT1);
        T2.swap(newT2);

        bool success = true;
        for (uint32_t storedKey : keys) {
            if (!placeKeyWithoutRehash(storedKey)) {
                success = false;
                break;
            }
        }

        if (success) {
            insertionsSinceRehash = 0;
            return true;
        }
    }

    return false;
}

bool CuckooHashing::rehashWithPendingKey(uint32_t key) {
    vector<uint32_t> keys = collectKeys();
    keys.push_back(key);
    return rebuildTablesWithKeys(keys);
}

void CuckooHashing::maybeForceRefreshRehash() {
    const long long rehashInterval = static_cast<long long>(tableSize) * tableSize;
    if (insertionsSinceRehash < rehashInterval) {
        return;
    }

    const vector<uint32_t> keys = collectKeys();
    if (!rebuildTablesWithKeys(keys)) {
        throw std::runtime_error("Failed forced rehash after reaching the paper's r^2 insertion refresh threshold");
    }
}

int CuckooHashing::countOccupiedSlots(const vector<uint32_t>& table) const {
    return static_cast<int>(std::count_if(table.begin(), table.end(),
        [](uint32_t value) { return value != EMPTY_SLOT; }));
}

int CuckooHashing::getTableSize() const {
    return tableSize;
}

int CuckooHashing::getTableBits() const {
    return tableBits;
}

int CuckooHashing::getMaxRehashLimit() const {
    return maxRehashLimit;
}

int CuckooHashing::getElementCount() const {
    return elementCount;
}

int CuckooHashing::getOccupiedCountT1() const {
    return countOccupiedSlots(T1);
}

int CuckooHashing::getOccupiedCountT2() const {
    return countOccupiedSlots(T2);
}

int CuckooHashing::getInsertAttempts() const {
    return insertAttempts;
}

int CuckooHashing::getSuccessfulInsertions() const {
    return successfulInsertions;
}

int CuckooHashing::getDuplicateInsertions() const {
    return duplicateInsertions;
}

int CuckooHashing::getRehashCount() const {
    return rehashCount;
}

int CuckooHashing::getFailedInsertions() const {
    return failedInsertions;
}

int CuckooHashing::getInsertionsSinceRehash() const {
    return insertionsSinceRehash;
}

long long CuckooHashing::getDisplacementCount() const {
    return displacementCount;
}

array<uint64_t, CuckooHashing::UNIVERSAL_HASH_COEFFICIENTS> CuckooHashing::getHash1Coefficients() const {
    return hash1Coefficients;
}

array<uint64_t, CuckooHashing::UNIVERSAL_HASH_COEFFICIENTS> CuckooHashing::getHash2Coefficients() const {
    return hash2Coefficients;
}

double CuckooHashing::getLoadFactor() const {
    const double totalSlots = static_cast<double>(2 * tableSize);
    return totalSlots == 0.0 ? 0.0 : static_cast<double>(elementCount) / totalSlots;
}

void CuckooHashing::printSummary() const {
    const array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> h1 = getHash1Coefficients();
    const array<uint64_t, UNIVERSAL_HASH_COEFFICIENTS> h2 = getHash2Coefficients();

    cout << "\n=== Cuckoo Hashing Summary ===" << endl;
    cout << "Parameters:" << endl;
    cout << "  Table size per table (rounded to power of two): " << tableSize << endl;
    cout << "  Table index bits q: " << tableBits << endl;
    cout << "  Total slots: " << (2 * tableSize) << endl;
    cout << "  Max displacement loop count: " << maxRehashLimit << endl;
    cout << "  Empty slot sentinel: " << EMPTY_SLOT << endl;
    cout << "  Universal hash prime p: " << UNIVERSAL_HASH_PRIME << endl;
    cout << "  Universal hash family: h(x) = (sum a_l*x^l mod p) mod r" << endl;
    cout << "  Polynomial degree: " << (UNIVERSAL_HASH_COEFFICIENTS - 1) << endl;
    cout << "  Cuckoo hash 1 coefficients: "
         << h1[0] << ", " << h1[1] << ", " << h1[2] << endl;
    cout << "  Cuckoo hash 2 coefficients: "
         << h2[0] << ", " << h2[1] << ", " << h2[2] << endl;
    cout << "  Forced refresh threshold: r^2 = "
         << (static_cast<long long>(tableSize) * tableSize) << " insertions" << endl;

    cout << "Runtime statistics:" << endl;
    cout << "  Active keys: " << elementCount << endl;
    cout << "  Occupied slots in T1: " << getOccupiedCountT1() << endl;
    cout << "  Occupied slots in T2: " << getOccupiedCountT2() << endl;
    cout << "  Load factor: " << fixed << setprecision(4) << getLoadFactor() << endl;
    cout << "  Insert attempts: " << insertAttempts << endl;
    cout << "  Successful insertions: " << successfulInsertions << endl;
    cout << "  Duplicate insertions ignored: " << duplicateInsertions << endl;
    cout << "  Failed insertions: " << failedInsertions << endl;
    cout << "  Rehash attempts: " << rehashCount << endl;
    cout << "  Insertions since last rehash: " << insertionsSinceRehash << endl;
    cout << "  Key displacements: " << displacementCount << endl;
}
