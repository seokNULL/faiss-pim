/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IndexBinaryFlat: exhaustive Hamming-distance kNN search.
// Reports search time, QPS and the package / DRAM energy of index.search().

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <omp.h>

#include <faiss/IndexBinaryFlat.h>

using idx_t = faiss::idx_t;

/* ----------------------------------------------------------------------
 * Intel RAPL energy counters.
 *
 *   /sys/class/powercap/intel-rapl:N                  package N
 *   /sys/class/powercap/intel-rapl:N/intel-rapl:N:M   subdomain (core/uncore/dram)
 *
 * energy_uj is a counter in microjoules that wraps at max_energy_range_uj.
 * On recent kernels it is root-readable only (CVE-2020-8694), so run as root
 * or:  sudo chmod -R a+r /sys/class/powercap/intel-rapl
 * ---------------------------------------------------------------------- */

static const char* RAPL_ROOT = "/sys/class/powercap";

static bool read_u64(const std::string& path, uint64_t& v) {
    std::ifstream f(path);
    return bool(f >> v);
}

static bool read_line(const std::string& path, std::string& s) {
    std::ifstream f(path);
    return bool(std::getline(f, s));
}

// Best-effort free memory in bytes, so an impossible database size fails with
// an explanation instead of an uncaught bad_alloc part-way through a sweep.
static uint64_t mem_available() {
    std::ifstream f("/proc/meminfo");
    std::string key;
    uint64_t kb = 0;
    while (f >> key >> kb) {
        if (key == "MemAvailable:") {
            return kb * 1024;
        }
        f.ignore(4096, '\n');
    }
    return 0;
}

struct Rapl {
    std::vector<std::string> pkg_path, dram_path;
    std::vector<uint64_t> pkg_max, dram_max;
    std::vector<uint64_t> pkg_prev, dram_prev;
    bool found = false;    // RAPL domains exist on this machine
    bool readable = false; // and energy_uj can actually be read

    void discover() {
        for (int i = 0;; i++) {
            std::string pdir =
                    std::string(RAPL_ROOT) + "/intel-rapl:" + std::to_string(i);
            std::string name;
            if (!read_line(pdir + "/name", name)) {
                break; // no more packages
            }
            found = true;

            uint64_t e = 0, mx = 0;
            if (!read_u64(pdir + "/energy_uj", e)) {
                continue; // present but not readable
            }
            read_u64(pdir + "/max_energy_range_uj", mx);
            pkg_path.push_back(pdir + "/energy_uj");
            pkg_max.push_back(mx);

            for (int j = 0;; j++) {
                std::string sdir = pdir + "/intel-rapl:" + std::to_string(i) +
                        ":" + std::to_string(j);
                std::string sname;
                if (!read_line(sdir + "/name", sname)) {
                    break;
                }
                if (sname != "dram") {
                    continue;
                }
                uint64_t se = 0, smx = 0;
                if (!read_u64(sdir + "/energy_uj", se)) {
                    continue;
                }
                read_u64(sdir + "/max_energy_range_uj", smx);
                dram_path.push_back(sdir + "/energy_uj");
                dram_max.push_back(smx);
            }
        }
        readable = !pkg_path.empty();
    }

    static void snapshot(
            const std::vector<std::string>& paths,
            std::vector<uint64_t>& out) {
        out.resize(paths.size());
        for (size_t i = 0; i < paths.size(); i++) {
            out[i] = 0;
            read_u64(paths[i], out[i]);
        }
    }

    static double joules(
            const std::vector<uint64_t>& now,
            const std::vector<uint64_t>& prev,
            const std::vector<uint64_t>& maxima) {
        uint64_t total = 0;
        for (size_t i = 0; i < now.size(); i++) {
            if (now[i] >= prev[i]) {
                total += now[i] - prev[i];
            } else if (maxima[i] > prev[i]) {
                total += maxima[i] - prev[i] + now[i]; // counter wrapped
            } else {
                total += now[i];
            }
        }
        return total / 1e6;
    }

    void start() {
        snapshot(pkg_path, pkg_prev);
        snapshot(dram_path, dram_prev);
    }

    void stop(double& pkg_j, double& dram_j) {
        std::vector<uint64_t> now;
        snapshot(pkg_path, now);
        pkg_j = joules(now, pkg_prev, pkg_max);
        snapshot(dram_path, now);
        dram_j = joules(now, dram_prev, dram_max);
    }
};

// Parses "1048576" or a K/M/G suffix, binary: 1K = 1024, 1M = 1024^2,
// 1G = 1024^3. Returns -1 on anything malformed.
static long long parse_size(const char* s) {
    char* end = nullptr;
    long long v = strtoll(s, &end, 10);
    if (end == s || v < 0) {
        return -1;
    }
    switch (*end) {
        case 'K':
        case 'k':
            v <<= 10;
            end++;
            break;
        case 'M':
        case 'm':
            v <<= 20;
            end++;
            break;
        case 'G':
        case 'g':
            v <<= 30;
            end++;
            break;
        default:
            break;
    }
    return (*end == '\0') ? v : -1;
}

int main(int argc, char** argv) {
    long long nt = 1;            // CPU threads
    long long d = 128;           // vector dimension in BITS, multiple of 8
    long long nb = 1024 * 1024;  // database vectors
    long long nq = 16;           // query vectors
    long long k = 1;             // neighbors per query
    long long runs = 1;          // search repetitions, averaged
    long long heap = 1;          // 1 = hammings_knn_hc, 0 = hammings_knn_mc

    for (int i = 1; i < argc; i++) {
        long long* target = nullptr;
        if (strcmp(argv[i], "-t") == 0) {
            target = &nt;
        } else if (strcmp(argv[i], "-d") == 0) {
            target = &d;
        } else if (strcmp(argv[i], "-nb") == 0) {
            target = &nb;
        } else if (strcmp(argv[i], "-nq") == 0) {
            target = &nq;
        } else if (strcmp(argv[i], "-k") == 0) {
            target = &k;
        } else if (strcmp(argv[i], "-r") == 0) {
            target = &runs;
        } else if (strcmp(argv[i], "-heap") == 0) {
            target = &heap;
        }
        if (target == nullptr || i + 1 >= argc) {
            printf("Usage: %s -t <threads> -d <bits> -nb <n> -nq <n> -k <n> "
                   "-r <runs> -heap <0|1>\n"
                   "Counts accept a binary K/M/G suffix, e.g. -nb 1G = %lld\n"
                   "-heap 1 selects the heap top-k, 0 the counting top-k\n",
                   argv[0], 1LL << 30);
            return 1;
        }
        *target = parse_size(argv[++i]);
        if (*target < 0) {
            printf("Error: %s expects a count, got '%s'\n", argv[i - 1], argv[i]);
            return 1;
        }
    }

    // IndexBinary stores d/8 bytes per vector.
    if (d <= 0 || d % 8 != 0) {
        printf("Error: -d must be a positive multiple of 8 (bits), got %lld\n", d);
        return 1;
    }
    if (nt <= 0 || nb <= 0 || nq <= 0 || k <= 0 || runs <= 0) {
        printf("Error: -t, -nb, -nq, -k and -r must be positive\n");
        return 1;
    }
    if (k > nb) {
        printf("Error: -k (%lld) cannot exceed -nb (%lld)\n", k, nb);
        return 1;
    }
    if (heap != 0 && heap != 1) {
        printf("Error: -heap must be 0 or 1, got %lld\n", heap);
        return 1;
    }

    omp_set_num_threads((int)nt);
    long long code_size = d / 8;

    printf("threads=%lld  d=%lld bits (%lld B)  nb=%lld  nq=%lld  k=%lld  "
           "runs=%lld  heap=%lld\n",
           nt, d, code_size, nb, nq, k, runs, heap);

    // hammings_knn_mc keeps one bucket per possible distance, each holding up
    // to k ids, for every query: nq * (d + 1) * k ids plus the counters. That
    // is unbounded in k and d, so account for it before allocating.
    const size_t db_bytes = (size_t)nb * code_size;
    size_t topk_bytes = 0;
    if (heap == 0) {
        size_t buckets = (size_t)d + 1;
        topk_bytes = (size_t)nq * buckets * ((size_t)k * sizeof(int64_t) +
                                             sizeof(int));
    }
    const double gib = 1024.0 * 1024.0 * 1024.0;
    uint64_t avail = mem_available();
    printf("database: %.2f GiB", db_bytes / gib);
    if (topk_bytes > 0) {
        printf(" + counting buckets: %.2f GiB", topk_bytes / gib);
    }
    printf(" (available: %.2f GiB)\n", avail / gib);
    if (avail > 0 && db_bytes + topk_bytes > avail) {
        printf("Error: need %.2f GiB but only %.2f GiB is available\n",
               (db_bytes + topk_bytes) / gib, avail / gib);
        return 1;
    }

    Rapl rapl;
    rapl.discover();
    if (!rapl.readable) {
        printf("Energy: RAPL %s\n",
               rapl.found ? "found but not readable, try: sudo chmod -R a+r "
                            "/sys/class/powercap/intel-rapl"
                          : "not available on this machine");
    }

    // 64 bits per draw rather than one byte: at these database sizes a
    // byte-at-a-time uniform_int_distribution dominates the build.
    std::mt19937_64 rng(1234);

    faiss::IndexBinaryFlat index(d);
    // false routes the scan through hammings_knn_mc (counting) instead of
    // hammings_knn_hc (heap).
    index.use_heap = (heap != 0);

    // The index keeps its own copy of what is handed to add(), so staging the
    // whole database first would double peak memory. Reserve once, then
    // generate and add in chunks.
    index.xb.reserve((size_t)nb * code_size);

    const long long chunk = 1 << 20;
    // Round up so the buffer is always a whole number of 64-bit words.
    std::vector<uint8_t> buf(
            (((size_t)std::min(chunk, nb) * code_size) + 7) & ~size_t(7));
    for (long long i = 0; i < nb; i += chunk) {
        long long n = std::min(chunk, nb - i);
        uint64_t* w = (uint64_t*)buf.data();
        for (size_t j = 0; j < buf.size() / 8; j++) {
            w[j] = rng();
        }
        index.add(n, buf.data());
    }

    std::vector<uint8_t> xq(((size_t)nq * code_size + 7) & ~size_t(7));
    uint64_t* wq = (uint64_t*)xq.data();
    for (size_t i = 0; i < xq.size() / 8; i++) {
        wq[i] = rng();
    }

    std::vector<int32_t> D((size_t)nq * k);
    std::vector<idx_t> I((size_t)nq * k);

    rapl.start();
    auto start = std::chrono::high_resolution_clock::now();
    for (long long r = 0; r < runs; r++) {
        index.search(nq, xq.data(), k, D.data(), I.data());
    }
    auto end = std::chrono::high_resolution_clock::now();
    double pkg_j = 0, dram_j = 0;
    rapl.stop(pkg_j, dram_j);

    // Everything below is per search: the totals divided by the run count.
    std::chrono::duration<double> elapsed = end - start;
    double total = elapsed.count();
    double sec = total / runs;
    pkg_j /= runs;
    dram_j /= runs;

    printf("Query Time: %.6f sec  (mean of %lld, %.3f sec measured)\n",
           sec, runs, total);
    printf("Queries per second (QPS): %.2f\n", nq / sec);

    char pkg_s[32] = "", dram_s[32] = "";
    if (rapl.readable) {
        snprintf(pkg_s, sizeof(pkg_s), "%.4f", pkg_j);
        snprintf(dram_s, sizeof(dram_s), "%.4f", dram_j);
        printf("Package Energy: %s J (%.2f W)\n", pkg_s, pkg_j / sec);
        if (!rapl.dram_path.empty()) {
            printf("DRAM Energy: %s J (%.2f W)\n", dram_s, dram_j / sec);
        } else {
            printf("DRAM Energy: no dram domain on this CPU\n");
            dram_s[0] = '\0';
        }
    }

    // One machine-readable line per run, consumed by run_cpu_binary_flat.sh.
    printf("CSV,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%.6f,%.2f,%s,%s\n",
           nt, d, nb, nq, k, heap, runs, sec, nq / sec, pkg_s, dram_s);

    for (long long i = 0; i < nq; i++) {
        printf("Query index[%lld]\n", i);
        for (long long j = 0; j < k; j++) {
            printf("Nearest idx: \t[%5zd], Hamming: \t%d\n",
                   I[i * k + j], D[i * k + j]);
        }
    }

    return 0;
}
