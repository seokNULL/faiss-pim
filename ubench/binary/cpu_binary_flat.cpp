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

int main(int argc, char** argv) {
    int nt = 1;           // CPU threads
    int d = 128;          // vector dimension in BITS, multiple of 8
    int nb = 1024 * 1024; // database vectors
    int nq = 16;          // query vectors
    int k = 1;            // neighbors per query

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            nt = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            d = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-nb") == 0 && i + 1 < argc) {
            nb = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-nq") == 0 && i + 1 < argc) {
            nq = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            k = atoi(argv[++i]);
        } else {
            printf("Usage: %s -t <threads> -d <bits> -nb <n> -nq <n> -k <n>\n",
                   argv[0]);
            return 1;
        }
    }

    // IndexBinary stores d/8 bytes per vector.
    if (d % 8 != 0) {
        printf("Error: -d must be a multiple of 8 (bits), got %d\n", d);
        return 1;
    }

    omp_set_num_threads(nt);
    int code_size = d / 8;

    printf("threads=%d  d=%d bits (%d B)  nb=%d  nq=%d  k=%d\n",
           nt, d, code_size, nb, nq, k);

    Rapl rapl;
    rapl.discover();
    if (!rapl.readable) {
        printf("Energy: RAPL %s\n",
               rapl.found ? "found but not readable, try: sudo chmod -R a+r "
                            "/sys/class/powercap/intel-rapl"
                          : "not available on this machine");
    }

    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> distrib(0, 255);

    std::vector<uint8_t> xb((size_t)nb * code_size);
    std::vector<uint8_t> xq((size_t)nq * code_size);

    for (size_t i = 0; i < xb.size(); i++) {
        xb[i] = distrib(rng);
    }
    for (size_t i = 0; i < xq.size(); i++) {
        xq[i] = distrib(rng);
    }

    faiss::IndexBinaryFlat index(d);
    index.add(nb, xb.data());

    std::vector<int32_t> D((size_t)nq * k);
    std::vector<idx_t> I((size_t)nq * k);

    rapl.start();
    auto start = std::chrono::high_resolution_clock::now();
    index.search(nq, xq.data(), k, D.data(), I.data());
    auto end = std::chrono::high_resolution_clock::now();
    double pkg_j = 0, dram_j = 0;
    rapl.stop(pkg_j, dram_j);

    std::chrono::duration<double> elapsed = end - start;
    double sec = elapsed.count();
    printf("Query Time: %.6f sec\n", sec);
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
    printf("CSV,%d,%d,%d,%d,%d,%.6f,%.2f,%s,%s\n",
           nt, d, nb, nq, k, sec, nq / sec, pkg_s, dram_s);

    for (int i = 0; i < nq; i++) {
        printf("Query index[%d]\n", i);
        for (int j = 0; j < k; j++) {
            printf("Nearest idx: \t[%5zd], Hamming: \t%d\n",
                   I[i * k + j], D[i * k + j]);
        }
    }

    return 0;
}
