/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// IndexBinaryFlat: exhaustive Hamming-distance kNN search.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <random>
#include <vector>

#include <omp.h>

#include <faiss/IndexBinaryFlat.h>

using idx_t = faiss::idx_t;

int main(int argc, char** argv) {
    int nt = 1;             // CPU threads
    int d = 128;            // vector dimension in BITS, multiple of 8
    int nb = 1024 * 1024;   // database vectors
    int nq = 16;            // query vectors
    int k = 1;              // neighbors per query

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

    auto start = std::chrono::high_resolution_clock::now();
    index.search(nq, xq.data(), k, D.data(), I.data());
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    printf("Query Time: %.6f sec\n", elapsed.count());
    printf("Queries per second (QPS): %.2f\n", nq / elapsed.count());

    for (int i = 0; i < nq; i++) {
        printf("Query index[%d]\n", i);
        for (int j = 0; j < k; j++) {
            printf("Nearest idx: \t[%5zd], Hamming: \t%d\n",
                   I[i * k + j], D[i * k + j]);
        }
    }

    return 0;
}
