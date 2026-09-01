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

## Energy

The package and DRAM energy consumed by `index.search()` is read from the
Intel RAPL counters in `/sys/class/powercap/intel-rapl`, summed over all
sockets. Counter wraparound at `max_energy_range_uj` is handled.

```
Package Energy: 12.3456 J (41.20 W)
DRAM Energy: 3.4567 J (11.53 W)
```

`energy_uj` is root-readable only on kernels since the CVE-2020-8694
mitigation, so either run as root or open it up (does not survive a reboot):

```sh
sudo chmod -R a+r /sys/class/powercap/intel-rapl
```

Without access the run still completes and prints why; the energy columns come
out empty.

Four things to keep in mind when reading the numbers:

- **RAPL is socket-wide.** It counts everything on the package, not just this
  process. Measure on an otherwise idle machine.
- **Idle power is included.** The counters do not subtract a baseline, so a
  search that lasts a few milliseconds is mostly idle draw. Use an `nb` and
  `nq` large enough to keep `index.search()` running for at least a second or
  two, otherwise the energy figure says more about the machine than about the
  search.
- **The counters update roughly every 1 ms**, which puts a floor on the
  resolution for the same reason.
- **The `dram` domain does not exist on every CPU.** Server parts expose it;
  many client parts only have `core`/`uncore`. The run prints
  `no dram domain on this CPU` and leaves that column empty.

## Sweeps

```sh
./run_cpu_binary_flat.sh                                   # writes results.csv
OUT=big.csv T_LIST="1 8" D_LIST=256 ./run_cpu_binary_flat.sh
```

The CSV columns are:

```
threads,d,nb,nq,k,time_s,qps,pkg_j,dram_j
```

Each run prints one `CSV,...` line that the script collects, so a single run
can be appended to a sheet by hand the same way.
