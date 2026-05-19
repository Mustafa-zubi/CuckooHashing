# A Study of Cuckoo Hashing vs. Robin Hood Hashing

This project implements and benchmarks two hash table schemes: Cuckoo Hashing and Robin Hood Hashing and compares them head to head. The benchmark workload follows the style used by Pagh and Rodler: load random keys first, then repeatedly run unsuccessful lookup, successful lookup, delete, and insert operations.

## What is included

- `benchmark_cuckoo_robin.cpp`: the benchmark source. It compares Cuckoo Hashing and Robin Hood Hashing.
- `CuckooHashing.h` / `CuckooHashing.cpp`: implementation of Cuckoo Hashing (two tables, kick-on-collision, doubling on forced rehash).
- `RobinHoodHashing.h` / `RobinHoodHashing.cpp`: implementation of Robin Hood Hashing (open addressing with a rich-loses-to-poor swap during insertion).
- `benchmark_cuckoo_robin`: the binary built from `benchmark_cuckoo_robin.cpp`.
- `run_benchmark_commands.py`: runs the benchmark commands many times, saves the averaged results to CSV, generates the cache miss sweep data, and renders the plots automatically.
- `plot_paper_graphs.py`: the plotting library used by `run_benchmark_commands.py`. It can also be invoked on its own.
- `docs/commands_cuckoo_robin`: the ready-to-run command list.

## Requirements

- A C++17 compiler, such as `g++`.
- `make`.
- Python 3 with `matplotlib`.

The random key dataset is the Marsaglia Diehard CD-ROM data. In this repo it is expected under `DiehardCDROM-master/CD-ROM/`, and the original dataset can be found in this repo also originally imported from here: [jeffThompson/DiehardCDROM](https://github.com/jeffThompson/DiehardCDROM/tree/master).

## Build

From the project folder, run:

```bash
make
```

This creates the binary:

```bash
./benchmark_cuckoo_robin
```

To clean the build files later:

```bash
make clean
```

## Quick Run

For a small test run of Cuckoo and Robin Hood:

```bash
./benchmark_cuckoo_robin DiehardCDROM-master/CD-ROM/bits.01 8192 24576 32768 64 100000 both 5000
```

In this command:

- `8192` is the number of initial keys, usually called `n`.
- `24576` is `3n`, the number of benchmark cycles.
- `32768` is the total table space, here `4n`.
- `64` is the maximum Cuckoo displacement loop.
- `100000` is the safety limit for rehash attempts.
- `both` runs both Cuckoo and Robin Hood. You can also pass `cuckoo` or `robinhood` to run just one.
- `5000` is the cycle threshold used to ignore likely interrupted operations.

## Output Folder

All outputs for a benchmark run live inside a single folder: the averaged CSV, the cache miss CSV, and every plot image.

- `docs/cuckoo_robin_graphs/`: holds the CSV(s) and every plot.

## Run the Prepared Experiments

The easiest way to run many experiments is to use the command list in `docs/`. The script runs the commands, saves the averaged CSV, generates the cache miss data, and renders all the plots in one step.

```bash
python3 run_benchmark_commands.py --commands-file docs/commands_cuckoo_robin --runs 10
```

The `--runs 10` option runs each benchmark command 10 times, then writes the average to the output CSV. This gives more stable timing results than a single run.

The script skips blank lines and commented lines in the command file.

Useful extra flags:

- `--output-dir <path>` overrides the default output folder.
- `--no-plot` writes only the CSV files and skips plotting.

After running, you should find these files inside `docs/cuckoo_robin_graphs/`:

- `cuckoo_robin_results.csv`: the averaged benchmark results.
- `cache_misses_data.csv`: the cache miss sweep data.
- `paper_style_graphs.png`: the combined benchmark plot with the main metrics.
- `successful_lookup.png`, `unsuccessful_lookup.png`, `insert.png`, `delete.png`, `update.png`, `average_lookup.png`, `throughput.png`: one PNG per metric.
- `paper_style_diagnostics.png`: load factor and key-moves-per-insert diagnostics.
- `cache_misses.png`: the cache miss curve for Cuckoo and Robin Hood.

## Plot the Results Manually

The plotting step happens automatically as part of `run_benchmark_commands.py`. You only need this section if you already have a CSV and want to re-plot it without re-running the benchmarks.

```bash
python3 plot_paper_graphs.py \
    --output-dir docs/cuckoo_robin_graphs \
    --csv docs/cuckoo_robin_graphs/cuckoo_robin_results.csv \
```

The `--output-dir` flag is the only one you usually need. The script puts every PNG into that folder.

## Cache Miss Sweep Mode

The benchmark binary also has a subcommand that sweeps the load factor and reports the average number of cache misses per insertion for Cuckoo and Robin Hood:

```bash
./benchmark_cuckoo_robin fig7 DiehardCDROM-master/CD-ROM/bits.01 32768 100000 100000 64 \
    > docs/cuckoo_robin_graphs/cache_misses_data.csv
```

The arguments are: input file, table size, warm-up mixed-op count, sample mixed-op count, and Cuckoo `maxLoop`.

## Working Environment Used

The experiments were developed on a node with two Intel Xeon Gold 5220R processors, 96 GB of RAM, and Red Hat Enterprise Linux 8.9. Storage was provided through GPFS rather than the local SSDs.
