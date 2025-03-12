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

#include <faiss/IndexPQFastScan.h>

using idx_t = faiss::idx_t;

int main() {
    int d = 256;     // dimension
    int nb = 1024;   // database size
    int nq = 5;    // nb of queries

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

    int m = 8;
    int n_bit = 4;

    faiss::IndexPQFastScan index(d, m, n_bit);

    index.verbose = true;

    printf("Index is trained? %s\n", index.is_trained ? "true" : "false");
    index.train(nb, xb);
    printf("Index is trained? %s\n", index.is_trained ? "true" : "false");
    index.add(nb, xb);

    int k = 4;

    { // search xq
        idx_t* nns_idx = new idx_t[(int)(k * nq)];
        float* nns_distance = new float[(int)(k * nq)];

        printf("  document_vec_ptr (xb) =  %p\n"
               "  qurey_vec_ptr (xq) = %p\n"
               "  nns_idx_ptr        = %p\n"
               "  nns_distance_ptr  = %p\n",
                    static_cast<void*>(xb), 
                    static_cast<void*>(xq), 
                    static_cast<void*>(nns_idx), 
                    static_cast<void*>(nns_distance)
                    );

        printf("Searching the %d nearest neighbors "
               "of %d vectors in the index\n",
               k,
               nq);

        index.search(nq, xq, k, nns_distance, nns_idx);
        
        for (int i = 0; i < nq; i++){
            printf("query %2d: ", i);
            for(int j = 0; j < k; j++){
                printf("%7ld ", nns_idx[j + i * k]); //Nearest seach result
            }
            printf("\n     dis: ");
            for (int j = 0; j < k; j++) {
                printf("%7g ", nns_distance[j + i * k]); //Distance for each neighbor
            }
            printf("\n");
        }
        delete[] nns_idx;
        delete[] nns_distance;
    }

    delete[] xb;
    delete[] xq;

    return 0;
} // namespace facebook::detail
