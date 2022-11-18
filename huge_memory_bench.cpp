#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include <fstream>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>

#include <linux/mman.h>
#include <sys/mman.h>

// README:
// Benchmark of initialization and random accesses in a big array of doubles.
// This was written to measure the impact of huge pages on processes doing
// memory accesses.
//
// Results will be very consistent if the program is run on a machine that has
// lots of memory (64GB+ recommended if using the default size of 32GB for the
// array) and is pretty idle. The array size can be overridden on the command
// line.
//
// To get proper results, make sure that the machine has enough free memory to
// store the entire array in memory using both 4K  pages and 2M.
// Before running, it is recommended to make the kernel drop its caches
// (echo 3 > /proc/sys/vm/drop_caches) and then do a compaction run (echo 1 >
// /proc/sys/vm/compact_memory).
//
// When using hugetlbfs, make sure there is enough huge pages on the node
// you'll be using (you need 16,000 huge pages if you're using the default size
// of 32 GiB):
// Run "head /sys/devices/system/node/node*/hugepages/*-2048kB/free_hugepages"
// to see the breakdown of free hugetlbfs pages per node (if running on a NUMA
// box)
//
// This program will create a ~1Gib file in /tmp (by default).  Make sure
// the machine has enough free disk space and remember to remove it when done measuring.
//
// Build:
// clang++/g++ -Wall -W -g -O2 -o huge_memory_bench huge_memory_bench.cpp

#if __cplusplus < 201103L
#error "Compile with -std=c++11 or later"
#endif

// Where we store the indices, so we do the same exact randomly generated
// accesses into the array every time
#define CACHED_INDICES_FILE "/tmp/mem_bench_indices"

using namespace std;

// Randomly generate a new list of indices to access for the benchmark
bool generateIndices(vector<unsigned long> *indices, unsigned long endIdx, unsigned long numIndices)
{
    FILE *fd = fopen(CACHED_INDICES_FILE, "w");
    if (!fd) {
        printf("Can't open %s for writing\n", CACHED_INDICES_FILE);
        return false;
    }

    mt19937_64 rng;
    random_device rdev;
    rng.seed(rdev());

    // Random indices into the array
    uniform_int_distribution<mt19937_64::result_type> dist(0, endIdx - 1);
    for (unsigned long i = 0; i < numIndices; ++i) {
        unsigned long idx = dist(rng);
        // Store both in the vector and the file so subsequent runs do the same
        // exact accesses
        fprintf(fd, "%lu\n", idx);
        indices->emplace_back(idx);
    }
    // Done
    fclose(fd);
    printf("Generated %s. Remember to remove it when done running benchmarks\n",
           CACHED_INDICES_FILE);
    return true;
}

// Try to read CACHED_INDICES_FILE. If it's missing or contains invalid data,
// generate a new one.
bool readIndices(vector<unsigned long> *indices, unsigned long endIdx,
                 unsigned long numIndices)
{
    // Reserve in advance so we don't do a gazillion reallocs.
    indices->reserve(numIndices);
    FILE *fd = fopen(CACHED_INDICES_FILE, "r");
    if (!fd) {
        // Most likely the file does not exist, generate a new one.
        return generateIndices(indices, endIdx, numIndices);
    }

    // Read the file line by line (one index per line)
    char buf[128];
    unsigned long maxIdx = 0;
    while (fgets(buf, sizeof(buf), fd)) {
        char *endPtr;
        long idx = strtol(buf, &endPtr, 10);

        // Validate idx
        if ((*endPtr != '\n' && *endPtr) || idx < 0
         || (unsigned long) idx >= endIdx) {
            printf("invalid line: %s", buf);
            break;
        }

        // Store in the vector
        indices->emplace_back(idx);
        maxIdx = max(maxIdx, (unsigned long)idx);
        if (indices->size() > numIndices)
            break;
    }
    fclose(fd);

    // Try to see if we generated indices for a different array size or if the
    // file was truncated or too big
    if ((endIdx - maxIdx) > 10000000 || indices->size() != numIndices) {
        // Get a new one.
        puts("Invalid file, regenerating");
        indices->clear();
        return generateIndices(indices, endIdx, numIndices);
    }
    return true;
}

void usage(char *name) {
    printf("Usage: %s [-h] [-s sizeInGib] [-m] [-t]\n", name);
    puts("Options");
    puts(" -h: display usage");
    puts(" -m: madvise the memory with MADV_HUGEPAGE (THP), conflicts with -t");
    puts(" -s sizeInGib: array size, default is 32Gib, max 128Gib");
    puts(" -t: allocate the array with MAP_HUGETLB (hugetlbfs), conflicts "
         "with -m");
    exit(1);
}

int main(int argc, char **argv)
{
    // Default size of the array: 32GiB, i.e 4Gi doubles
    unsigned long arraySize = 32U*1024UL*1024UL*1024UL;

    // One past last valid index in the array.
    unsigned long endIdx = arraySize / sizeof(double);

    bool hugetlb = false, thp = false;
    char opt;
    while ((opt = getopt(argc, argv, "hmts:")) != -1) {
        switch (opt) {
            case 'h':
                usage(argv[0]);
                break;
            case 't':
                hugetlb = true;
                break;
            case 'm': {
                thp = true;
                ifstream ifs("/sys/kernel/mm/transparent_hugepage/enabled");
                bool enabled = false;
                if (ifs) {
                    string str;
                    if (getline(ifs, str)
                      && str.find("[never]") == string::npos) {
                        enabled = true;
                    }
                }
                if (!enabled) {
                    puts("Tranparent Huge Pages are not enabled. Switch "
                         "/sys/kernel/mm/transparent_hugepage to either "
                         "madvise or always (madvise recommended for this "
                         "test)");
                    return 1;
                }
            } break;
            case 's': {
                // Override the array size using the 1st arg if present.  It's
                // passed in GiB.
                char *endPtr;
                const unsigned long num  = strtoul(optarg, &endPtr, 0);
                if (*endPtr || num > 128) {
                    // No more than 128 GiB. It's arbitrary to avoid passing
                    // really large amounts.
                    usage(argv[0]);
                }
                arraySize = num * 1024UL * 1024UL * 1024UL;
                endIdx = arraySize / sizeof(double);
                break;
            }
            default: usage(argv[0]);
        }
    }
    if ((hugetlb & thp) || optind != argc) {
        usage(argv[0]);
    }

    // Number of accesses into the array we'll bench: 3% of the total
    unsigned long numIndices = endIdx * 0.03;

    vector<unsigned long> indices;
    puts("Getting the indices");
    if (!readIndices(&indices, endIdx, numIndices)) {
        puts("Can't get indices");
        return 1;
    }

    int flags = MAP_ANONYMOUS | MAP_PRIVATE;
    if (hugetlb)
        flags |= MAP_HUGETLB | MAP_HUGE_2MB;
    void * const mem = mmap(nullptr, arraySize, PROT_READ|PROT_WRITE, flags,
            -1, 0);
    if (mem == MAP_FAILED) {
        perror("Cannot allocate memory!");
        if (hugetlb) {
            puts("You must have at least 16Gi free hugetlbfs pages. "
                 "Check /proc/meminfo to see the number of free hugetlbfs"
                 "pages and adjust if necessary with hugeadm");
        }
        return 1;
    }
    if (thp) {
        if (madvise(mem, arraySize, MADV_HUGEPAGE)) {
            puts("madvise MADV_HUGEPAGE failed, enable THP and try again");
            return 1;
        }
    }

    double * const array = (double *) mem;

    // Initialize the array. You won't see a dramatic difference in terms of
    // performance between 4K and 2MB pages because the array is initialized
    // linearly.
    puts("Initializing the array");
    chrono::time_point<chrono::system_clock> startTime, endTime;
    startTime = chrono::system_clock::now();
    asm volatile ("" ::: "memory");
    for (unsigned long i = 0; i < endIdx; ++i) {
        // We're going to add a lot of doubles so we generate fairly small
        // numbers.
        array[i] = 1e-9 * double(i % 79);
    }
    asm volatile ("" ::: "memory");
    endTime = chrono::system_clock::now();
    chrono::duration<double> elapsed = endTime - startTime;
    printf("Initialization of the array took %.4lf secs\n", elapsed.count());

    double result = 0.0;

    // What we're really timing: randomly generated accesses into the double
    // array.  We're computing result to make sure all runs are consistent but
    // also so the compiler does not get too clever and removes the code
    // we're trying to measure.

    startTime = chrono::system_clock::now();
    asm volatile ("" ::: "memory");
    for (unsigned long u : indices) {
        result += array[u];
    }
    asm volatile ("" ::: "memory");
    endTime = chrono::system_clock::now();

    elapsed = endTime - startTime;

    printf("Adding took %.4lf secs\n", elapsed.count());
    // The result is interesting just to double check that every run is
    // adding the same doubles.
    printf("Result is %lf\n", result);

    munmap(mem, arraySize);

    return 0;
}
