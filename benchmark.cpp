#include "CuckooHashing.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

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
    int cycles = 5000;
    int tableSizePerTable = 32768;
    int maxLoop = 64;
    int maxAllowedRehashes = 10000;
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
};

template <typename Operation>
double timeOperationSeconds(Operation&& operation) {
    const auto start = std::chrono::steady_clock::now();
    operation();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

void printOperationTiming(const string& label, int count, double seconds) {
    cout << label << ": " << count << " ops in "
         << fixed << setprecision(6) << seconds << " s";

    if (count > 0) {
        const double averageSeconds = seconds / static_cast<double>(count);
        cout << " | avg "
             << fixed << setprecision(2) << (averageSeconds * 1e9)
             << " ns/op";
    }

    cout << endl;
}

void enforceRehashGuard(
    int completedCycles,
    const BenchmarkConfig& config,
    const CuckooHashing& table) {
    const int rehashCount = table.getRehashCount();
    if (rehashCount <= config.maxAllowedRehashes) {
        return;
    }

    throw std::runtime_error(
        "Aborting benchmark after " + std::to_string(completedCycles) +
        " cycles because rehash attempts reached " + std::to_string(rehashCount) +
        ", exceeding the configured safety limit of " +
        std::to_string(config.maxAllowedRehashes));
}

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
        if (paths.size() == 1) {
            return paths.front();
        }
        return paths.front() + " ... " + paths.back();
    }

    std::size_t getFileCount() const {
        return paths.size();
    }

    std::uintmax_t getTotalWordCount() const {
        return totalWordCount;
    }

private:
    void initializePaths(const string& path) {
        const fs::path requestedPath(path);

        if (fs::is_directory(requestedPath)) {
            for (const auto& entry : fs::directory_iterator(requestedPath)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                const string filename = entry.path().filename().string();
                if (filename.rfind("bits.", 0) == 0 || filename == "calif.bit" ||
                    filename == "canada.bit" || filename == "germany.bit") {
                    paths.push_back(entry.path().string());
                }
            }
            std::sort(paths.begin(), paths.end());
        } else {
            paths.push_back(requestedPath.string());

            const string filename = requestedPath.filename().string();
            if (filename.rfind("bits.", 0) == 0) {
                vector<string> siblingBits;
                for (const auto& entry : fs::directory_iterator(requestedPath.parent_path())) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const string siblingName = entry.path().filename().string();
                    if (siblingName.rfind("bits.", 0) == 0) {
                        siblingBits.push_back(entry.path().string());
                    }
                }

                std::sort(siblingBits.begin(), siblingBits.end());
                const auto startIt = std::find(siblingBits.begin(), siblingBits.end(), requestedPath.string());
                if (startIt != siblingBits.end()) {
                    paths.assign(startIt, siblingBits.end());
                    paths.insert(paths.end(), siblingBits.begin(), startIt);
                }
            }
        }

        if (paths.empty()) {
            throw std::runtime_error("No input files found for benchmark path: " + path);
        }
        else{
            for (const string& i : paths) {
                cout << "Discovered input file: " << i << endl;
            }   
        }

        totalWordCount = 0;
        for (const string& currentPath : paths) {
            const auto fileSize = fs::file_size(currentPath);
            totalWordCount += (fileSize / sizeof(uint32_t));
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
        const uint32_t rawValue = reader.nextWord();
        ++stats.valuesRead;

        if (rawValue != 0u && rawValue <= static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            return rawValue;
        }
    }
}

vector<uint32_t> buildInitialSet(
    DiehardWordReader& reader,
    BenchmarkStats& stats,
    int targetSize,
    const std::chrono::steady_clock::time_point& stageStart) {
    unordered_set<uint32_t> seen; // nice to know, it uses hash tables. 
    vector<uint32_t> keys;
    seen.reserve(targetSize * 2);
    keys.reserve(targetSize);
    // const int progressInterval = std::max(1, targetSize / 20);
    std::uintmax_t readsWithoutGrowth = 0;

    while (static_cast<int>(keys.size()) < targetSize) {
        const uint32_t key = nextPositiveKey(reader, stats);
        ++readsWithoutGrowth;
        if (seen.insert(key).second) {
            keys.push_back(key);
            readsWithoutGrowth = 0;
            const int currentSize = static_cast<int>(keys.size());
            // if (currentSize % progressInterval == 0 || currentSize == targetSize) {
            //     printStageProgress("initial-set", currentSize, targetSize, stats, nullptr, stageStart);
            // }
        } else {
            ++stats.duplicateCandidates;
        }

        if (readsWithoutGrowth > reader.getTotalWordCount()) {
            throw std::runtime_error(
                "Unable to grow the initial set after scanning the entire selected input corpus once. "
                "Requested initialKeys=" + std::to_string(targetSize) +
                ", current unique keys=" + std::to_string(keys.size()) +
                ". Use more CD-ROM files or reduce the initial set size.");
        }
    }

    return keys;
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;

    if (argc > 1) {
        config.inputPath = argv[1];
    }
    if (argc > 2) {
        config.initialKeys = std::stoi(argv[2]);
    }
    if (argc > 3) {
        config.cycles = std::stoi(argv[3]);
    }
    if (argc > 4) {
        config.tableSizePerTable = std::stoi(argv[4]);
    }
    if (argc > 5) {
        config.maxLoop = std::stoi(argv[5]);
    }
    if (argc > 6) {
        config.maxAllowedRehashes = std::stoi(argv[6]);
    }

    if (config.initialKeys <= 0 || config.cycles <= 0 ||
        config.tableSizePerTable <= 0 || config.maxLoop <= 0 ||
        config.maxAllowedRehashes <= 0) {
        throw std::invalid_argument("All numeric benchmark parameters must be positive");
    }

    BenchmarkStats stats;
    DiehardWordReader reader(config.inputPath);
    const auto setupStart = std::chrono::steady_clock::now();

    cout << "=== benchmark Startup ===" << endl;
    cout << "Input source: " << reader.getPathLabel() << endl;
    cout << "input files discovered: " << reader.getFileCount() << endl;
    cout << "total 32 bit words available: " << reader.getTotalWordCount() << endl;
    cout << "Initial keys target: " << config.initialKeys << endl;
    cout << "benchmark cycles target: " << config.cycles << endl;
    cout << "Table siez (per table): " << config.tableSizePerTable << endl;
    cout << "Max loop count: " << config.maxLoop << endl;
    cout << "Rehash saftey limit: " << config.maxAllowedRehashes << endl;

    vector<uint32_t> activeKeys = buildInitialSet(reader, stats, config.initialKeys, setupStart);
    // vector<uint32> activeKeys = buildInitialSet(reader1, stats, config.initialKeys, setupStart);
    unordered_set<uint32_t> activeSet(activeKeys.begin(), activeKeys.end());
    activeSet.reserve(activeKeys.size() * 2);

    CuckooHashing table(config.tableSizePerTable, config.maxLoop);
    // const int initialInsertProgressInterval = std::max(1, config.initialKeys / 20);
    for (std::size_t i = 0; i < activeKeys.size(); ++i) {
        const uint32_t key = activeKeys[i];
        table.insert(static_cast<int>(key));
        ++stats.insertions;
        const int completedInitialInserts = static_cast<int>(i + 1);
        enforceRehashGuard(completedInitialInserts, config, table);
        // if (completedInitialInserts % initialInsertProgressInterval == 0 ||
        //     completedInitialInserts == config.initialKeys) {
        //     printStageProgress(
        //         "initial-insert",
        //         completedInitialInserts,
        //         config.initialKeys,
        //         stats,
        //         &table,
        //         setupStart);
        // }
    }

    const auto benchmarkStart = std::chrono::steady_clock::now();
    // const int progressInterval = std::max(1, config.cycles / 20);

    for (int cycle = 0; cycle < config.cycles; ++cycle) {
        const uint32_t missingKey = nextPositiveKey(reader, stats);
        if (activeSet.find(missingKey) != activeSet.end()) {
            ++stats.duplicateCandidates;
        } else {
            ++stats.unsuccessfulLookups;
            stats.unsuccessfulLookupSeconds += timeOperationSeconds([&]() {
                (void)table.search(static_cast<int>(missingKey));
            });
        }

        const uint32_t successfulKey = activeKeys[cycle % activeKeys.size()];
        ++stats.successfulLookups;
        stats.successfulLookupSeconds += timeOperationSeconds([&]() {
            (void)table.search(static_cast<int>(successfulKey));
        });

        const std::size_t deleteIndex = static_cast<std::size_t>(cycle % activeKeys.size());
        const uint32_t deletedKey = activeKeys[deleteIndex];
        stats.deletionSeconds += timeOperationSeconds([&]() {
            table.remove(static_cast<int>(deletedKey));
        });
        activeSet.erase(deletedKey);
        ++stats.deletions;

        uint32_t insertedKey = nextPositiveKey(reader, stats);
        while (activeSet.find(insertedKey) != activeSet.end()) {
            ++stats.duplicateCandidates;
            insertedKey = nextPositiveKey(reader, stats);
        }

        stats.insertionSeconds += timeOperationSeconds([&]() {
            table.insert(static_cast<int>(insertedKey));
        });
        activeKeys[deleteIndex] = insertedKey;
        activeSet.insert(insertedKey);
        ++stats.insertions;

        const int completedCycles = cycle + 1;
        enforceRehashGuard(completedCycles, config, table);
        // if (completedCycles % progressInterval == 0 || completedCycles == config.cycles) {
        //     printProgress(completedCycles, config.cycles, stats, table, benchmarkStart);
        // }
    }

    const auto benchmarkEnd = std::chrono::steady_clock::now();
    stats.elapsedSeconds = std::chrono::duration<double>(benchmarkEnd - benchmarkStart).count();
    stats.remainingKeys = table.getElementCount();

    cout << "=== Benchmark Summary ===" << endl;
    cout << "Input file: " << config.inputPath << endl;
    cout << "Initial keys loaded: " << config.initialKeys << endl;
    cout << "Benchmark cycles: " << config.cycles << endl;
    cout << "Configured table size per table: " << config.tableSizePerTable << endl;
    cout << "Configured max loop count: " << config.maxLoop << endl;
    cout << "Configured rehash safety limit: " << config.maxAllowedRehashes << endl;
    cout << "Random values consumed: " << stats.valuesRead << endl;
    cout << "Duplicate candidate values skipped: " << stats.duplicateCandidates << endl;
    cout << "Successful lookups executed: " << stats.successfulLookups << endl;
    cout << "Unsuccessful lookups executed: " << stats.unsuccessfulLookups << endl;
    cout << "Deletions executed: " << stats.deletions << endl;
    cout << "Insertions executed: " << stats.insertions << endl;
    cout << "Remaining active keys: " << stats.remainingKeys << endl;
    cout << "Elapsed time (seconds): " << fixed << setprecision(6) << stats.elapsedSeconds << endl;
    cout << "\n=== Per-Operation Timing ===" << endl;
    printOperationTiming("Successful Lookup", stats.successfulLookups, stats.successfulLookupSeconds);
    printOperationTiming("Unsuccessful Lookup", stats.unsuccessfulLookups, stats.unsuccessfulLookupSeconds);
    printOperationTiming("Delete", stats.deletions, stats.deletionSeconds);
    printOperationTiming("Insert", stats.insertions, stats.insertionSeconds);
    if (stats.elapsedSeconds > 0.0) {
        const double operations =
            static_cast<double>(stats.successfulLookups + stats.unsuccessfulLookups +
                                stats.deletions + stats.insertions);
        cout << "Operations per second: " << fixed << setprecision(2)
             << (operations / stats.elapsedSeconds) << endl;
    }

    table.printSummary();
    return 0;
}