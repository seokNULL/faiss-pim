/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IndexBinaryFlat micro-benchmark: exhaustive Hamming-distance kNN search.
//
// Database and query vectors are packed bit vectors: d bits -> d/8 bytes.
// Queries are planted copies of database vectors with a known number of bit
// flips, so the expected nearest neighbour is known and recall can be checked.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <faiss/IndexBinaryFlat.h>

using idx_t = faiss::idx_t;
using clk = std::chrono::high_resolution_clock;

namespace {

void usage(const char* prog) {
    std::fprintf(
            stderr,
            "Usage: %s [options]\n"
            "  -d      <bits>   vector dimension in BITS, multiple of 8   (default 128)\n"
            "  -nb     <n>      number of database vectors                (default 1048576)\n"
            "  -nq     <n>      number of query vectors                   (default 16)\n"
            "  -k      <n>      number of neighbours to return            (default 1)\n"
            "  -r      <n>      timed runs, the fastest is reported       (default 5)\n"
            "  -flip   <n>      bits flipped when planting a query        (default d/32, min 1)\n"
            "  -heap   <0|1>    1 = heap top-k, 0 = counting top-k        (default 1)\n"
            "  -batch  <n>      query_batch_size of the index             (default 32)\n"
            "  -seed   <n>      RNG seed                                  (default 1234)\n"
            "  -v               print per-query results\n"
            "  -h               this message\n",
            prog);
}

// Parses "-flag value". Returns false if the value is missing or malformed.
bool take_long(int argc, char** argv, int& i, long& out) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s needs a value\n", argv[i]);
        return false;
    }
    char* end = nullptr;
    long v = std::strtol(argv[++i], &end, 10);
    if (end == argv[i] || *end != '\0') {
        std::fprintf(stderr, "error: %s expects an integer, got '%s'\n",
                     argv[i - 1], argv[i]);
        return false;
    }
    out = v;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    long d = 128;           // vector dimension in BITS
    long nb = 1024 * 1024;  // database vectors
    long nq = 16;           // query vectors
    long k = 1;             // neighbours per query
    long num_runs = 5;      // timed runs
    long nflip = -1;        // bits flipped per planted query, -1 = auto
    long use_heap = 1;      // 1 = hammings_knn_hc, 0 = hammings_knn_mc
    long batch = 32;        // IndexBinaryFlat::query_batch_size
    long seed = 1234;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool ok = true;
        if (a == "-d") {
            ok = take_long(argc, argv, i, d);
        } else if (a == "-nb") {
            ok = take_long(argc, argv, i, nb);
        } else if (a == "-nq") {
            ok = take_long(argc, argv, i, nq);
        } else if (a == "-k") {
            ok = take_long(argc, argv, i, k);
        } else if (a == "-r") {
            ok = take_long(argc, argv, i, num_runs);
        } else if (a == "-flip") {
            ok = take_long(argc, argv, i, nflip);
        } else if (a == "-heap") {
            ok = take_long(argc, argv, i, use_heap);
        } else if (a == "-batch") {
            ok = take_long(argc, argv, i, batch);
        } else if (a == "-seed") {
            ok = take_long(argc, argv, i, seed);
        } else if (a == "-v") {
            verbose = true;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
            ok = false;
        }
        if (!ok) {
            usage(argv[0]);
            return 1;
        }
    }

    // IndexBinary stores d/8 bytes per vector, so d must be a multiple of 8.
    if (d <= 0 || d % 8 != 0) {
        std::fprintf(stderr,
                     "error: -d must be a positive multiple of 8 (bits), got %ld\n", d);
        return 1;
    }
    if (nb <= 0 || nq <= 0 || k <= 0 || num_runs <= 0 || batch <= 0) {
        std::fprintf(stderr, "error: -nb, -nq, -k, -r and -batch must be positive\n");
        return 1;
    }
    if (k > nb) {
        std::fprintf(stderr, "error: -k (%ld) cannot exceed -nb (%ld)\n", k, nb);
        return 1;
    }
    if (nflip < 0) {
        nflip = std::max(1L, d / 32);
    }
    if (nflip > d) {
        std::fprintf(stderr, "error: -flip (%ld) cannot exceed -d (%ld)\n", nflip, d);
        return 1;
    }

    const size_t code_size = static_cast<size_t>(d) / 8;
    const double db_bytes = static_cast<double>(nb) * code_size;

    std::printf("[config] d=%ld bits (%zu B/vec)  nb=%ld  nq=%ld  k=%ld  "
                "flip=%ld  heap=%ld  batch=%ld  runs=%ld\n",
                d, code_size, nb, nq, k, nflip, use_heap, batch, num_runs);
    std::printf("[config] database = %.2f MiB\n", db_bytes / (1024.0 * 1024.0));

    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::uniform_int_distribution<int> byte_dist(0, 255);

    // ---- database: uniformly random bit vectors ----
    std::vector<uint8_t> xb(static_cast<size_t>(nb) * code_size);
    for (auto& b : xb) {
        b = static_cast<uint8_t>(byte_dist(rng));
    }

    // ---- queries: copy of a random database vector with nflip bits flipped ----
    // ground_truth[i] is then the expected nearest neighbour of query i.
    std::vector<uint8_t> xq(static_cast<size_t>(nq) * code_size);
    std::vector<idx_t> ground_truth(nq);
    std::uniform_int_distribution<long> id_dist(0, nb - 1);
    std::uniform_int_distribution<long> bit_dist(0, d - 1);

    for (long i = 0; i < nq; i++) {
        long src = id_dist(rng);
        ground_truth[i] = static_cast<idx_t>(src);
        uint8_t* q = xq.data() + static_cast<size_t>(i) * code_size;
        std::memcpy(q, xb.data() + static_cast<size_t>(src) * code_size, code_size);

        // Flip nflip distinct bit positions so the planted distance is exact.
        std::vector<long> pos;
        pos.reserve(nflip);
        while (static_cast<long>(pos.size()) < nflip) {
            long p = bit_dist(rng);
            if (std::find(pos.begin(), pos.end(), p) == pos.end()) {
                pos.push_back(p);
            }
        }
        for (long p : pos) {
            q[p / 8] ^= static_cast<uint8_t>(1u << (p % 8));
        }
    }

    // ---- build ----
    faiss::IndexBinaryFlat index(d);
    index.use_heap = (use_heap != 0);
    index.query_batch_size = static_cast<size_t>(batch);

    auto t_add0 = clk::now();
    index.add(nb, xb.data());
    auto t_add1 = clk::now();
    std::printf("[build]  add: %.4f s  (ntotal=%lld)\n",
                std::chrono::duration<double>(t_add1 - t_add0).count(),
                static_cast<long long>(index.ntotal));

    std::vector<int32_t> D(static_cast<size_t>(nq) * k);
    std::vector<idx_t> I(static_cast<size_t>(nq) * k);

    // ---- warm-up (page-in the database, prime the caches) ----
    index.search(nq, xq.data(), k, D.data(), I.data());

    // ---- timed runs ----
    double best = 1e30, total = 0.0;
    for (long r = 0; r < num_runs; r++) {
        auto t0 = clk::now();
        index.search(nq, xq.data(), k, D.data(), I.data());
        auto t1 = clk::now();
        double e = std::chrono::duration<double>(t1 - t0).count();
        best = std::min(best, e);
        total += e;
    }

    const double avg = total / num_runs;
    // Every query scans the whole database, so this is the byte volume the
    // search has to stream per run.
    const double scanned = db_bytes * static_cast<double>(nq);

    std::printf("[search] best: %.6f s | avg: %.6f s\n", best, avg);
    std::printf("[search] QPS (best): %.2f | per-query: %.6f ms\n",
                nq / best, best * 1000.0 / nq);
    std::printf("[search] scan rate: %.2f GB/s\n",
                scanned / best / 1e9);

    // ---- recall against the planted ground truth ----
    long hits = 0;
    double dist_sum = 0.0;
    for (long i = 0; i < nq; i++) {
        for (long j = 0; j < k; j++) {
            if (I[i * k + j] == ground_truth[i]) {
                hits++;
                break;
            }
        }
        dist_sum += D[i * k];
    }
    std::printf("[check]  recall@%ld: %.4f (%ld/%ld) | mean top-1 distance: %.2f "
                "(planted %ld)\n",
                k, static_cast<double>(hits) / nq, hits, nq,
                dist_sum / nq, nflip);

    if (verbose) {
        for (long i = 0; i < nq; i++) {
            std::printf("query[%ld] gt=%lld\n", i,
                        static_cast<long long>(ground_truth[i]));
            for (long j = 0; j < k; j++) {
                std::printf("  #%ld idx=%8lld  hamming=%d\n", j,
                            static_cast<long long>(I[i * k + j]), D[i * k + j]);
            }
        }
    }

    return 0;
}
