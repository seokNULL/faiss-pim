/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <chrono>
#include <iostream>

#include <faiss/IndexPQFastScan.h>
#include <faiss/IndexPQ.h>

using idx_t = faiss::idx_t;

int main(int argc, char** argv) {
    int d = 128;          // Vector dimension
    int nb = 1024 * 1024; // Number of database vectors
    int nq = 16;          // Number of query vectors
    int k = 1;            // Number of nearest neighbors
    int num_runs = 10;     // Number of runs for averaging QPS

    int m = 8;             // d/m = dimension of subvector
    int n_bit = 8;        // 2^n_bit = #of centroids

    // Ensure correct number of arguments
    if (argc < 7) {
        std::cerr << "Usage: " << argv[0] << " -nb <num_db_vectors> -nq <num_queries> -k <num_neighbors>" << std::endl;
        return 1;
    }

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-nb" && i + 1 < argc) {
            nb = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "-nq" && i + 1 < argc) {
            nq = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "-k" && i + 1 < argc) {
            k = std::stoi(argv[++i]);
        }
    }

    std::mt19937 rng;
    std::uniform_real_distribution<> distrib;

    float* xb = new float[(int)(d * nb)];
    float* xq = new float[(int)(d * nq)];

    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < d; j++) {
            xb[d * i + j] = distrib(rng);
        }
        xb[d * i] += i / 1000.;
    }

    for (int i = 0; i < nq; i++) {
        for (int j = 0; j < d; j++) {
            xq[d * i + j] = distrib(rng);
        }
        xq[d * i] += i / 1000.;
    }

    
    faiss::IndexPQ index(d, m, n_bit);
    faiss::IndexPQStats stats;

    // index.verbose = true;
    // printf("Index is trained? %s\n", index.is_trained ? "true" : "false");
    index.train(nb, xb);
    // printf("Index is trained? %s\n", index.is_trained ? "true" : "false");
    index.add(nb, xb);

    { // Perform search multiple times and compute average QPS
        idx_t* I = new idx_t[k * nq];
        float* D = new float[k * nq];

        double total_qps = 0.0;
        for (int run = 0; run < num_runs; run++) {
          auto start = std::chrono::high_resolution_clock::now();
            index.search(nq, xq, k, D, I);
          auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            double qps = nq / elapsed.count();
            total_qps += qps;
        }

        double avg_qps = total_qps / num_runs;
        printf("Average QPS over %d runs: %.2f\n", num_runs, avg_qps);

        delete[] I;
        delete[] D;
    }

    delete[] xb;
    delete[] xq;

    return 0;
} // namespace facebook::detail
