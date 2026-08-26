# Binary index micro-benchmarks

`IndexBinaryFlat` — exhaustive Hamming-distance kNN over packed bit vectors.

## Build

```sh
make
```

`libfaiss.a` is a static archive, so it carries no record of its own
dependencies: BLAS and LAPACK have to be named on the link line, after
`-lfaiss`. Without them the link fails on `sgemm_`, `dsyev_`, `dgesvd_`,
`sgeqrf_` and friends. The Makefile probes for OpenBLAS, then reference
LAPACK/BLAS, then MKL, and stops with an explanation if it finds none:

```sh
sudo apt install libopenblas-dev liblapack-dev
```

Override the probe when it picks the wrong one — a conda BLAS is not on the
default search path, for instance:

```sh
make BLAS_LIBS="-L$CONDA_PREFIX/lib -lopenblas"
make BLAS_LIBS="-lmkl_rt"                        # MKL
make FAISS_PREFIX=/opt/faiss                     # faiss installed elsewhere
```

Those symbols come from parts of faiss the binary path never calls (PCA, OPQ,
float distances), but a static link still has to resolve them.

## Run

```sh
./cpu_binary_flat -d 256 -nb 4194304 -nq 128 -k 16
```

| flag | meaning | default |
|---|---|---|
| `-d` | vector dimension **in bits**, must be a multiple of 8 | 128 |
| `-nb` | database vectors | 1048576 |
| `-nq` | query vectors | 16 |
| `-k` | neighbours per query | 1 |
| `-r` | timed runs; the fastest is reported | 5 |
| `-flip` | bits flipped when planting a query | `d/32`, min 1 |
| `-heap` | `1` = heap top-k (`hammings_knn_hc`), `0` = counting top-k (`hammings_knn_mc`) | 1 |
| `-batch` | `IndexBinaryFlat::query_batch_size` | 32 |
| `-seed` | RNG seed | 1234 |
| `-v` | print per-query neighbours | off |

`d` is a **bit** count, not a float count: the index stores `d/8` bytes per
vector and rejects any `d` that is not a multiple of 8.

## Sweeps

```sh
./run_cpu_binary_flat.sh                          # default d/nb/nq/k grid
D_LIST="256 512" NB_LIST=4194304 ./run_cpu_binary_flat.sh
```

## Correctness check

Each query is a copy of a randomly chosen database vector with `-flip` bits
flipped, so its nearest neighbour is known by construction. The benchmark
reports `recall@k` against that planted ground truth and the mean top-1
distance, which should equal `-flip`.

With uniformly random bit vectors, two unrelated vectors sit at an expected
Hamming distance of `d/2`, so a small `-flip` keeps the planted neighbour well
clear of the noise floor and recall should read 1.0. Raising `-flip` toward
`d/2` erodes that margin — useful for probing how top-k behaves when candidate
distances bunch up.
