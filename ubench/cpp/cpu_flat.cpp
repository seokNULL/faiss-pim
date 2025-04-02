#include <cstdio>
#include <cstdlib>
#include <random>
#include <iostream>
#include <chrono>
#include <faiss/IndexFlat.h>

void print_info(const float* x, int n_col, int n_row) {
    for (int i = 0; i < n_row; i++) {
        std::cout << "Data[" << i << "]"<< ": ";
        for (int j = 0; j < n_col; j++) {
            std::cout << x[i * n_col + j] << " ";
        }
        std::cout << std::endl;
    }
}

using idx_t = faiss::idx_t;

int main(int argc, char** argv) {
    // Default values
    int d = 128;          // Vector dimension
    int nb = 1024 * 1024; // Number of database vectors
    int nq = 16;          // Number of query vectors
    int k = 16;           // Number of nearest neighbors
    int num_runs = 1;    // Number of runs for averaging QPS

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

    // Generate random database vectors
    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < d; j++)
            xb[d * i + j] = distrib(rng);
        xb[d * i] += i / 1000.0;
    }

    // Generate random query vectors
    for (int i = 0; i < nq; i++) {
        for (int j = 0; j < d; j++)
            xq[d * i + j] = distrib(rng);
        xq[d * i] += i / 1000.0;
    }

    // Create FAISS index with L2 distance
    faiss::IndexFlatL2 index(d);
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

    // Free allocated memory
    delete[] xb;
    delete[] xq;

    return 0;
}
