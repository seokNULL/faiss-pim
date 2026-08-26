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
./cpu_binary_flat -t 8 -d 256 -nb 4194304 -nq 128 -k 16
```

| flag | meaning | default |
|---|---|---|
| `-t` | CPU threads | 1 |
| `-d` | vector dimension **in bits**, must be a multiple of 8 | 128 |
| `-nb` | database vectors | 1048576 |
| `-nq` | query vectors | 16 |
| `-k` | neighbours per query | 1 |

`d` is a **bit** count, not a float count: the index stores `d/8` bytes per
vector and rejects any `d` that is not a multiple of 8.

## Sweeps

```sh
./run_cpu_binary_flat.sh
T_LIST="1 8" D_LIST="256 512" NB_LIST=4194304 ./run_cpu_binary_flat.sh
```
