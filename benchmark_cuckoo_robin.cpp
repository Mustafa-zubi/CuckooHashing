// Focused two-way comparison: Cuckoo Hashing vs Robin Hood Hashing.
//
// Reuses the equilibrium workload methodology from Pagh & Rodler (2004):
// load n distinct random keys from the Marsaglia Diehard CD-ROM, then run
// `cycles` iterations of {unsuccessful lookup, successful lookup, delete,
// insert}. Per-op clock cycles are measured with rdtsc and the >5000-cycle
// interrupt filter is applied (paper §4.3).
//
// Output format is identical to benchmark.cpp so that run_benchmark_commands.py
// and plot_paper_graphs.py can consume both binaries' results without changes.

#include "CuckooHashing.h"
#include "RobinHoodHashing.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

using std::cout;
using std::endl;
using std::fixed;
using std::setprecision;
using std::string;
using std::uint32_t;
using std::unordered_set;
using std::vector;
namespace fs = std::filesystem;

struct BenchmarkConfig {
    string inputPath = "DiehardCDROM-master/CD-ROM/bits.01";
    int initialKeys = 10000;
    int cycles = 30000;
    int tableSizePerTable = 32768;
    int maxLoop = 64;
    int maxAllowedRehashes = 100000;
    string algorithm = "both";
    unsigned long long interruptCycleThreshold = 5000;
};

struct BenchmarkStats {
    int valuesRead = 0;
    int duplicateCandidates = 0;
    int unsuccessfulLookups = 0;
    int successfulLookups = 0;
    int deletions = 0;
    int insertions = 0;
    int remainingKeys = 0;
    double elapsedSeconds = 0.0;
    double successfulLookupSeconds = 0.0;
    double unsuccessfulLookupSeconds = 0.0;
    double deletionSeconds = 0.0;
    double insertionSeconds = 0.0;
    unsigned long long successfulLookupCycles = 0;
    unsigned long long unsuccessfulLookupCycles = 0;
    unsigned long long deletionCycles = 0;
    unsigned long long insertionCycles = 0;
    int filteredSuccessfulLookups = 0;
    int filteredUnsuccessfulLookups = 0;
    int filteredDeletions = 0;
    int filteredInsertions = 0;
    int droppedSuccessfulLookups = 0;
    int droppedUnsuccessfulLookups = 0;
    int droppedDeletions = 0;
    int droppedInsertions = 0;
};

struct WorkloadOperation {
    uint32_t missingLookupKey = 0;
    std::size_t successfulLookupIndex = 0;
    std::size_t deleteIndex = 0;
    uint32_t insertedKey = 0;
};

struct BenchmarkWorkload {
    vector<uint32_t> initialKeys;
    vector<WorkloadOperation> operations;
    BenchmarkStats generationStats;
};

struct OperationTiming {
    double seconds = 0.0;
    unsigned long long cycles = 0;
};

unsigned long long readCycleCounter() {
#if defined(__i386__) || defined(__x86_64__)
    _mm_lfence();
    const unsigned long long cycles = __rdtsc();
    _mm_lfence();
    return cycles;
#else
    return static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

unsigned long long estimateCycleReadOverhead() {
    unsigned long long best = std::numeric_limits<unsigned long long>::max();
    for (int i = 0; i < 1000; ++i) {
        const unsigned long long start = readCycleCounter();
        const unsigned long long end = readCycleCounter();
        best = std::min(best, end - start);
    }
    return best == std::numeric_limits<unsigned long long>::max() ? 0 : best;
}

template <typename Operation>
OperationTiming timeOperation(Operation&& operation, unsigned long long cycleReadOverhead) {
    const auto start = std::chrono::steady_clock::now();
    const unsigned long long startCycles = readCycleCounter();
    operation();
    const unsigned long long endCycles = readCycleCounter();
    const auto end = std::chrono::steady_clock::now();

    OperationTiming timing;
    timing.seconds = std::chrono::duration<double>(end - start).count();
    const unsigned long long rawCycles = endCycles - startCycles;
    timing.cycles = rawCycles > cycleReadOverhead ? rawCycles - cycleReadOverhead : 0;
    return timing;
}

void printOperationTiming(
    const string& label,
    int count,
    double seconds,
    unsigned long long cycles,
    int filteredCount,
    int droppedCount) {
    cout << label << ": " << count << " ops in "
         << fixed << setprecision(6) << seconds << " s";
    if (count > 0) {
        const double averageSeconds = seconds / static_cast<double>(count);
        cout << " | avg "
             << fixed << setprecision(2) << (averageSeconds * 1e9)
             << " ns/op";
        if (filteredCount > 0) {
            cout << " | avg "
                 << fixed << setprecision(2)
                 << (static_cast<double>(cycles) / static_cast<double>(filteredCount))
                 << " cycles/op";
        }
    }
    cout << " | filtered ops " << filteredCount << " | dropped " << droppedCount << endl;
}

struct FilterResult { bool keep; };

template <typename GetEventCount>
FilterResult applyCycleFilter(
    unsigned long long cycles,
    unsigned long long threshold,
    long long eventCountBefore,
    GetEventCount&& getEventCountAfter) {
    if (cycles < threshold) return {true};
    if (getEventCountAfter() != eventCountBefore) return {true};
    return {false};
}

// Both schemes either resize-on-insert (cuckoo) or have no structural events
// (robin hood). We model both with a structural-event counter that the filter
// inspects; for robin hood that counter is constant 0.
inline long long structuralEventCount(const CuckooHashing& t) { return t.getRehashCount(); }
inline long long structuralEventCount(const RobinHoodHashing&) { return 0; }

class DiehardWordReader {
public:
    explicit DiehardWordReader(const string& path) {
        initializePaths(path);
        openCurrentFile();
    }

    uint32_t nextWord() {
        uint32_t value = 0;
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (input.gcount() == static_cast<std::streamsize>(sizeof(value))) {
            return value;
        }
        advanceToNextFile();
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (input.gcount() != static_cast<std::streamsize>(sizeof(value))) {
            throw std::runtime_error("Input corpus does not contain enough data: " + getPathLabel());
        }
        return value;
    }

    string getPathLabel() const {
        if (paths.size() == 1) return paths.front();
        return paths.front() + " ... " + paths.back();
    }

    std::size_t getFileCount() const { return paths.size(); }
    std::uintmax_t getTotalWordCount() const { return totalWordCount; }

private:
    void initializePaths(const string& path) {
        const fs::path requested(path);
        if (fs::is_directory(requested)) {
            for (const auto& entry : fs::directory_iterator(requested)) {
                if (!entry.is_regular_file()) continue;
                const string name = entry.path().filename().string();
                if (name.rfind("bits.", 0) == 0 || name == "calif.bit" ||
                    name == "canada.bit" || name == "germany.bit") {
                    paths.push_back(entry.path().string());
                }
            }
            std::sort(paths.begin(), paths.end());
        } else {
            paths.push_back(requested.string());
            const string filename = requested.filename().string();
            if (filename.rfind("bits.", 0) == 0) {
                vector<string> siblings;
                for (const auto& entry : fs::directory_iterator(requested.parent_path())) {
                    if (!entry.is_regular_file()) continue;
                    const string n = entry.path().filename().string();
                    if (n.rfind("bits.", 0) == 0) siblings.push_back(entry.path().string());
                }
                std::sort(siblings.begin(), siblings.end());
                const auto startIt = std::find(siblings.begin(), siblings.end(), requested.string());
                if (startIt != siblings.end()) {
                    paths.assign(startIt, siblings.end());
                    paths.insert(paths.end(), siblings.begin(), startIt);
                }
            }
        }
        if (paths.empty()) {
            throw std::runtime_error("No input files found for benchmark path: " + path);
        }
        totalWordCount = 0;
        for (const string& p : paths) {
            totalWordCount += (fs::file_size(p) / sizeof(uint32_t));
        }
    }

    void openCurrentFile() {
        input.close();
        input.clear();
        input.open(paths[currentPathIndex], std::ios::binary);
        if (!input) {
            throw std::runtime_error("Failed to open input file: " + paths[currentPathIndex]);
        }
    }

    void advanceToNextFile() {
        currentPathIndex = (currentPathIndex + 1) % paths.size();
        openCurrentFile();
    }

    std::ifstream input;
    vector<string> paths;
    std::size_t currentPathIndex = 0;
    std::uintmax_t totalWordCount = 0;
};

uint32_t nextPositiveKey(DiehardWordReader& reader, BenchmarkStats& stats) {
    while (true) {
        const uint32_t raw = reader.nextWord();
        ++stats.valuesRead;
        if (raw != 0u && raw <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            return raw;
        }
    }
}

vector<uint32_t> buildInitialSet(
    DiehardWordReader& reader,
    BenchmarkStats& stats,
    int targetSize) {
    unordered_set<uint32_t> seen;
    vector<uint32_t> keys;
    seen.reserve(targetSize * 2);
    keys.reserve(targetSize);
    std::uintmax_t readsWithoutGrowth = 0;

    while (static_cast<int>(keys.size()) < targetSize) {
        const uint32_t key = nextPositiveKey(reader, stats);
        ++readsWithoutGrowth;
        if (seen.insert(key).second) {
            keys.push_back(key);
            readsWithoutGrowth = 0;
        } else {
            ++stats.duplicateCandidates;
        }
        if (readsWithoutGrowth > reader.getTotalWordCount()) {
            throw std::runtime_error(
                "Unable to grow the initial set after scanning the entire input corpus once.");
        }
    }
    return keys;
}

BenchmarkWorkload buildBenchmarkWorkload(
    DiehardWordReader& reader,
    const BenchmarkConfig& config) {
    BenchmarkWorkload workload;
    workload.initialKeys = buildInitialSet(reader, workload.generationStats, config.initialKeys);
    workload.operations.reserve(static_cast<std::size_t>(config.cycles));

    vector<uint32_t> activeKeys = workload.initialKeys;
    unordered_set<uint32_t> activeSet(activeKeys.begin(), activeKeys.end());
    activeSet.reserve(activeKeys.size() * 2);

    // Independent random indices for successful lookup vs deletion (paper
    // §4.4). Reusing the same index for both makes the delete operate on a
    // cache-hot slot, masking the cache-miss penalty at large n.
    std::random_device indexSeed;
    std::mt19937_64 indexRng(indexSeed());
    std::uniform_int_distribution<std::size_t> indexDist(0, activeKeys.size() - 1);

    for (int cycle = 0; cycle < config.cycles; ++cycle) {
        WorkloadOperation op;
        op.missingLookupKey = nextPositiveKey(reader, workload.generationStats);
        while (activeSet.find(op.missingLookupKey) != activeSet.end()) {
            ++workload.generationStats.duplicateCandidates;
            op.missingLookupKey = nextPositiveKey(reader, workload.generationStats);
        }
        op.successfulLookupIndex = indexDist(indexRng);
        op.deleteIndex = indexDist(indexRng);

        const uint32_t deletedKey = activeKeys[op.deleteIndex];
        activeSet.erase(deletedKey);

        op.insertedKey = nextPositiveKey(reader, workload.generationStats);
        while (activeSet.find(op.insertedKey) != activeSet.end()) {
            ++workload.generationStats.duplicateCandidates;
            op.insertedKey = nextPositiveKey(reader, workload.generationStats);
        }
        activeKeys[op.deleteIndex] = op.insertedKey;
        activeSet.insert(op.insertedKey);
        workload.operations.push_back(op);
    }
    return workload;
}

template <typename Table>
BenchmarkStats runWorkloadOnTable(
    Table& table,
    const BenchmarkConfig& config,
    const BenchmarkWorkload& workload,
    unsigned long long cycleReadOverhead) {
    BenchmarkStats stats = workload.generationStats;
    vector<uint32_t> activeKeys = workload.initialKeys;
    unordered_set<uint32_t> activeSet(activeKeys.begin(), activeKeys.end());
    activeSet.reserve(activeKeys.size() * 2);

    for (uint32_t k : activeKeys) {
        table.insert(static_cast<int>(k));
        ++stats.insertions;
    }

    const unsigned long long threshold = config.interruptCycleThreshold;
    const auto benchmarkStart = std::chrono::steady_clock::now();

    for (std::size_t cycle = 0; cycle < workload.operations.size(); ++cycle) {
        const WorkloadOperation& op = workload.operations[cycle];

        ++stats.unsuccessfulLookups;
        long long evBefore = structuralEventCount(table);
        OperationTiming t = timeOperation([&]() {
            (void)table.search(static_cast<int>(op.missingLookupKey));
        }, cycleReadOverhead);
        stats.unsuccessfulLookupSeconds += t.seconds;
        if (applyCycleFilter(t.cycles, threshold, evBefore,
                             [&]() { return structuralEventCount(table); }).keep) {
            stats.unsuccessfulLookupCycles += t.cycles;
            ++stats.filteredUnsuccessfulLookups;
        } else {
            ++stats.droppedUnsuccessfulLookups;
        }

        const uint32_t successfulKey = activeKeys[op.successfulLookupIndex];
        ++stats.successfulLookups;
        evBefore = structuralEventCount(table);
        t = timeOperation([&]() {
            (void)table.search(static_cast<int>(successfulKey));
        }, cycleReadOverhead);
        stats.successfulLookupSeconds += t.seconds;
        if (applyCycleFilter(t.cycles, threshold, evBefore,
                             [&]() { return structuralEventCount(table); }).keep) {
            stats.successfulLookupCycles += t.cycles;
            ++stats.filteredSuccessfulLookups;
        } else {
            ++stats.droppedSuccessfulLookups;
        }

        const uint32_t deletedKey = activeKeys[op.deleteIndex];
        evBefore = structuralEventCount(table);
        t = timeOperation([&]() {
            table.remove(static_cast<int>(deletedKey));
        }, cycleReadOverhead);
        stats.deletionSeconds += t.seconds;
        if (applyCycleFilter(t.cycles, threshold, evBefore,
                             [&]() { return structuralEventCount(table); }).keep) {
            stats.deletionCycles += t.cycles;
            ++stats.filteredDeletions;
        } else {
            ++stats.droppedDeletions;
        }
        activeSet.erase(deletedKey);
        ++stats.deletions;

        evBefore = structuralEventCount(table);
        t = timeOperation([&]() {
            table.insert(static_cast<int>(op.insertedKey));
        }, cycleReadOverhead);
        stats.insertionSeconds += t.seconds;
        if (applyCycleFilter(t.cycles, threshold, evBefore,
                             [&]() { return structuralEventCount(table); }).keep) {
            stats.insertionCycles += t.cycles;
            ++stats.filteredInsertions;
        } else {
            ++stats.droppedInsertions;
        }
        activeKeys[op.deleteIndex] = op.insertedKey;
        activeSet.insert(op.insertedKey);
        ++stats.insertions;
    }

    const auto benchmarkEnd = std::chrono::steady_clock::now();
    stats.elapsedSeconds = std::chrono::duration<double>(benchmarkEnd - benchmarkStart).count();
    stats.remainingKeys = table.getElementCount();
    return stats;
}

void printBenchmarkSummary(
    const string& algorithmName,
    const BenchmarkConfig& config,
    const BenchmarkStats& stats) {
    cout << "=== Benchmark Summary ===" << endl;
    cout << "Algorithm: " << algorithmName << endl;
    cout << "Input file: " << config.inputPath << endl;
    cout << "Initial keys loaded: " << config.initialKeys << endl;
    cout << "Benchmark cycles: " << config.cycles << endl;
    cout << "Configured table size per table: " << config.tableSizePerTable << endl;
    cout << "Configured max loop count: " << config.maxLoop << endl;
    cout << "Configured rehash safety limit: " << config.maxAllowedRehashes << endl;
    cout << "Configured target total slots: " << config.tableSizePerTable << endl;
    cout << "Random values consumed: " << stats.valuesRead << endl;
    cout << "Duplicate candidate values skipped: " << stats.duplicateCandidates << endl;
    cout << "Successful lookups executed: " << stats.successfulLookups << endl;
    cout << "Unsuccessful lookups executed: " << stats.unsuccessfulLookups << endl;
    cout << "Deletions executed: " << stats.deletions << endl;
    cout << "Insertions executed: " << stats.insertions << endl;
    cout << "Remaining active keys: " << stats.remainingKeys << endl;
    cout << "Elapsed time (seconds): " << fixed << setprecision(6) << stats.elapsedSeconds << endl;
    cout << "Interrupt cycle filter: drop ops > "
         << config.interruptCycleThreshold << " cycles when no rehash occurred" << endl;
    cout << "\n=== Per-Operation Timing ===" << endl;
    printOperationTiming("Successful Lookup", stats.successfulLookups,
                         stats.successfulLookupSeconds, stats.successfulLookupCycles,
                         stats.filteredSuccessfulLookups, stats.droppedSuccessfulLookups);
    printOperationTiming("Unsuccessful Lookup", stats.unsuccessfulLookups,
                         stats.unsuccessfulLookupSeconds, stats.unsuccessfulLookupCycles,
                         stats.filteredUnsuccessfulLookups, stats.droppedUnsuccessfulLookups);
    printOperationTiming("Delete", stats.deletions, stats.deletionSeconds,
                         stats.deletionCycles, stats.filteredDeletions, stats.droppedDeletions);
    printOperationTiming("Insert", stats.insertions, stats.insertionSeconds,
                         stats.insertionCycles, stats.filteredInsertions, stats.droppedInsertions);
    if (stats.elapsedSeconds > 0.0) {
        const double ops = static_cast<double>(
            stats.successfulLookups + stats.unsuccessfulLookups +
            stats.deletions + stats.insertions);
        cout << "Operations per second: " << fixed << setprecision(2)
             << (ops / stats.elapsedSeconds) << endl;
    }
}

// Pagh & Rodler Fig 7 mini-driver: sweep load factor α ∈ {0.05…0.45} and
// report average cache-line accesses per insertion for Cuckoo and Robin Hood
// only. Mirrors the four-scheme sweep in benchmark.cpp's fig7 mode.
template <typename Table>
double runFig7Phase(
    Table& table,
    DiehardWordReader& reader,
    BenchmarkStats& stats,
    int targetN,
    int warmupOps,
    int sampleOps,
    unsigned rngSeed) {
    unordered_set<uint32_t> active;
    active.reserve(static_cast<std::size_t>(targetN) * 2);
    vector<uint32_t> activeKeys;
    activeKeys.reserve(targetN);

    while (static_cast<int>(activeKeys.size()) < targetN) {
        const uint32_t key = nextPositiveKey(reader, stats);
        if (active.insert(key).second) {
            table.insert(static_cast<int>(key));
            activeKeys.push_back(key);
        }
    }

    std::mt19937 rng(rngSeed);
    std::uniform_int_distribution<int> idxDist(0, targetN - 1);

    auto mixedOp = [&]() {
        const int idx = idxDist(rng);
        const uint32_t removed = activeKeys[idx];
        table.remove(static_cast<int>(removed));
        active.erase(removed);
        uint32_t replacement;
        do {
            replacement = nextPositiveKey(reader, stats);
        } while (!active.insert(replacement).second);
        table.insert(static_cast<int>(replacement));
        activeKeys[idx] = replacement;
    };

    for (int i = 0; i < warmupOps; ++i) mixedOp();
    table.resetInsertCellAccessStats();
    for (int i = 0; i < sampleOps; ++i) mixedOp();
    return table.getAverageInsertCellAccesses();
}

int runFig7Sweep(
    DiehardWordReader& reader,
    int perTableSize,
    int warmupOps,
    int sampleOps,
    int maxLoop) {
    const double targetAlphas[] = {
        0.05, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45};
    const std::size_t alphaCount = sizeof(targetAlphas) / sizeof(targetAlphas[0]);

    cout << "algorithm,alpha,n,total_slots,avg_cell_accesses_per_insert,"
         << "sampled_inserts" << endl;

    BenchmarkStats stats;
    const int totalSlots = 2 * perTableSize;

    auto emitRow = [&](const string& algo, double alpha, int n, double avg) {
        cout << algo << ","
             << fixed << setprecision(4) << alpha << ","
             << n << ","
             << totalSlots << ","
             << setprecision(6) << avg << ","
             << sampleOps << endl;
    };

    for (std::size_t a = 0; a < alphaCount; ++a) {
        const double alpha = targetAlphas[a];
        const int targetN = static_cast<int>(alpha * totalSlots);
        const unsigned seed =
            static_cast<unsigned>(targetN) ^
            static_cast<unsigned>(a) ^ 0x9E3779B9u;

        {
            CuckooHashing table(perTableSize, maxLoop);
            const double avg = runFig7Phase(table, reader, stats, targetN,
                                            warmupOps, sampleOps, seed);
            emitRow("cuckoo", alpha, table.getElementCount(), avg);
        }
        {
            RobinHoodHashing table(totalSlots);
            const double avg = runFig7Phase(table, reader, stats, targetN,
                                            warmupOps, sampleOps, seed);
            emitRow("robinhood", alpha, table.getElementCount(), avg);
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;

    // Subcommand dispatch: ./benchmark_cuckoo_robin fig7 <input>
    //     <perTableSize> <warmup> <samples> <maxLoop>
    if (argc > 1 && std::string(argv[1]) == "fig7") {
        const string input = argc > 2 ? argv[2] : config.inputPath;
        const int perTable = argc > 3 ? std::stoi(argv[3]) : 32768;
        const int warmup = argc > 4 ? std::stoi(argv[4]) : 100000;
        const int samples = argc > 5 ? std::stoi(argv[5]) : 100000;
        const int loop = argc > 6 ? std::stoi(argv[6]) : 64;
        DiehardWordReader reader(input);
        return runFig7Sweep(reader, perTable, warmup, samples, loop);
    }

    if (argc > 1) config.inputPath = argv[1];
    if (argc > 2) config.initialKeys = std::stoi(argv[2]);
    if (argc > 3) config.cycles = std::stoi(argv[3]);
    if (argc > 4) config.tableSizePerTable = std::stoi(argv[4]);
    if (argc > 5) config.maxLoop = std::stoi(argv[5]);
    if (argc > 6) config.maxAllowedRehashes = std::stoi(argv[6]);
    if (argc > 7) config.algorithm = argv[7];
    if (argc > 8) config.interruptCycleThreshold =
        static_cast<unsigned long long>(std::stoull(argv[8]));

    if (config.initialKeys <= 0 || config.cycles <= 0 ||
        config.tableSizePerTable <= 0 || config.maxLoop <= 0 ||
        config.maxAllowedRehashes <= 0) {
        throw std::invalid_argument("All numeric benchmark parameters must be positive");
    }
    if (config.algorithm != "cuckoo" && config.algorithm != "robinhood" &&
        config.algorithm != "robin" && config.algorithm != "both") {
        throw std::invalid_argument("Algorithm must be one of: cuckoo, robinhood, robin, both");
    }

    DiehardWordReader reader(config.inputPath);

    cout << "=== benchmark_cuckoo_robin Startup ===" << endl;
    cout << "Input source: " << reader.getPathLabel() << endl;
    cout << "Input files discovered: " << reader.getFileCount() << endl;
    cout << "Total 32-bit words available: " << reader.getTotalWordCount() << endl;
    cout << "Initial keys target: " << config.initialKeys << endl;
    cout << "Benchmark cycles target: " << config.cycles << endl;
    cout << "Table size (per table for cuckoo / total for robin hood): "
         << config.tableSizePerTable << endl;
    cout << "Max loop count (cuckoo): " << config.maxLoop << endl;
    cout << "Rehash safety limit: " << config.maxAllowedRehashes << endl;
    cout << "Algorithm mode: " << config.algorithm << endl;

    const BenchmarkWorkload workload = buildBenchmarkWorkload(reader, config);
    const unsigned long long cycleReadOverhead = estimateCycleReadOverhead();
    cout << "Random values consumed while building shared workload: "
         << workload.generationStats.valuesRead << endl;
    cout << "Duplicate candidate values skipped while building shared workload: "
         << workload.generationStats.duplicateCandidates << endl;
    cout << "Measured cycle counter overhead: " << cycleReadOverhead << " cycles" << endl;

    if (config.algorithm == "cuckoo" || config.algorithm == "both") {
        CuckooHashing table(std::max(1, (config.tableSizePerTable + 1) / 2), config.maxLoop);
        const BenchmarkStats stats = runWorkloadOnTable(table, config, workload, cycleReadOverhead);
        printBenchmarkSummary("cuckoo", config, stats);
        table.printSummary();
    }

    if (config.algorithm == "robinhood" || config.algorithm == "robin" ||
        config.algorithm == "both") {
        RobinHoodHashing table(config.tableSizePerTable);
        const BenchmarkStats stats = runWorkloadOnTable(table, config, workload, cycleReadOverhead);
        printBenchmarkSummary("robinhood", config, stats);
        table.printSummary();
    }

    return 0;
}
