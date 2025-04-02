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
#include <iostream>

#include <faiss/IndexFlat.h>
#include <faiss/gpu/GpuIndexFlat.h>
#include <faiss/gpu/GpuIndexIVFFlat.h>
#include <faiss/gpu/StandardGpuResources.h>

int main(int argc, char** argv) {
    // Default values
    int d = 128;          // Vector dimension
    int nb = 1024 * 1024; // Number of database vectors
    int nq = 16;          // Number of query vectors
    int k = 16;           // Number of nearest neighbors
    int num_runs = 10;    // Number of runs for averaging QPS

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

    float* xb = new float[d * nb];
    float* xq = new float[d * nq];

    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < d; j++)
            xb[d * i + j] = distrib(rng);
        xb[d * i] += i / 1000.;
    }

    for (int i = 0; i < nq; i++) {
        for (int j = 0; j < d; j++)
            xq[d * i + j] = distrib(rng);
        xq[d * i] += i / 1000.;
    }

    faiss::gpu::StandardGpuResources res;

    // Using a flat index
    faiss::gpu::GpuIndexFlatL2 index_flat(&res, d);
    index_flat.add(nb, xb); // add vectors to the index
    


    { // search xq
        long* I = new long[k * nq];
        float* D = new float[k * nq];

        double total_qps = 0.0;
        for (int run = 0; run < num_runs; run++) {
            auto start = std::chrono::high_resolution_clock::now();
            index_flat.search(nq, xq, k, D, I);
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


    // Free allocated memory
    delete[] xb;
    delete[] xq;

    return 0;
}
