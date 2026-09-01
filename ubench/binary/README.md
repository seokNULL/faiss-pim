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

### Which faiss library to link

`libfaiss.a` is the **generic** build: faiss compiles it without `-mavx2` or
`-mpopcnt`, so `popcount64` becomes software SWAR and the Hamming kernels run
3-4x slower than they need to. Building faiss with `FAISS_OPT_LEVEL` installs
`libfaiss_avx2.a` / `libfaiss_avx512.a` *alongside* the generic one — it does
not replace it, and the C++ static libraries carry no runtime dispatch, so the
library has to be named explicitly:

```sh
make FAISS_LIB=-lfaiss_avx512
make FAISS_LIB=-lfaiss_avx2
```

Measured on a 2M x d database, `nq=16`, 4 threads:

| d | generic | avx2 | avx512 |
|---|---|---|---|
| 128 | 0.0553 s | 0.0190 s (2.9x) | 0.0118 s (4.7x) |
| 512 | 0.2208 s | 0.0513 s (4.3x) | 0.0514 s (4.3x) |

At `d=512` avx512 matches avx2 because `HammingComputer64` only uses
`_mm512_popcnt_epi64` under `__AVX512VPOPCNTDQ__`, which faiss's stock avx512
flags do not define. Build faiss with `-DFAISS_ENABLE_AVX512_VPOPCNTDQ=ON`
(Ice Lake and later) to enable it.

### BLAS

Override the probe when it picks the wrong one — a conda BLAS is not on the
default search path, for instance:

```sh
make BLAS_LIBS="-L$CONDA_PREFIX/lib -lopenblas"
make BLAS_LIBS="-lmkl_rt"                        # MKL
make FAISS_PREFIX=/opt/faiss                     # faiss installed elsewhere
```

All three combine, e.g.

```sh
make FAISS_LIB=-lfaiss_avx512 BLAS_LIBS="-L$CONDA_PREFIX/lib -lopenblas"
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
| `-r` | search repetitions; time and energy are the mean | 1 |

`d` is a **bit** count, not a float count: the index stores `d/8` bytes per
vector and rejects any `d` that is not a multiple of 8.

Every count takes an optional binary K/M/G suffix, so `-nb 1G` is 1073741824
vectors (`1024^3`), `-nb 4M` is 4194304, `-nb 64K` is 65536. Case does not
matter. The database is generated and added in chunks, so peak memory stays at
the index's own copy rather than double it — but the database still has to fit
in RAM. The run prints its size against `MemAvailable` and exits with an
explanation if it cannot fit, rather than dying on `bad_alloc` part-way through
a sweep.

| `-nb` | `-d 64` | `-d 128` | `-d 256` |
|---|---|---|---|
| 1G | 8 GiB | 16 GiB | 32 GiB |
| 16G | 128 GiB | 256 GiB | 512 GiB |
| 64G | 512 GiB | 1 TiB | 2 TiB |
| 256G | 2 TiB | 4 TiB | 8 TiB |

There is no arithmetic limit before that: counts are 64-bit and byte totals are
`size_t`, whose 8 EiB ceiling is far above anything that fits in memory. RAM is
the only wall. Generating the database runs at roughly 3 GiB/s, so a 1 TiB
database costs about 6 minutes of build time before the first search.

`-r` repeats `index.search()` and reports the mean time and mean energy per
search. It is the knob for stretching the RAPL measurement window without
growing the database.

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
  search that lasts a few milliseconds is mostly idle draw. Raise `-r` (or
  `nb`/`nq`) until the measured window is at least a second or two, otherwise
  the energy figure says more about the machine than about the search. The run
  prints the total measured time next to the mean so the window is visible.
- **The counters update roughly every 1 ms**, which puts a floor on the
  resolution for the same reason.
- **The `dram` domain does not exist on every CPU.** Server parts expose it;
  many client parts only have `core`/`uncore`. The run prints
  `no dram domain on this CPU` and leaves that column empty.

## Sweeps

```sh
./run_cpu_binary_flat.sh                                   # writes results.csv
OUT=big.csv T_LIST="1 8" NB_LIST="1G" RUNS=10 ./run_cpu_binary_flat.sh
```

Each result is printed as it lands, so a long sweep can be watched:

```
threads     d            nb    nq      k  runs      time_s        qps     GB/s     pkg_J    dram_J
----------------------------------------------------------------------------------------------
      4   128       1048576     8      1     2    0.005325    1502.38    25.21         -         -
      4   512       4194304     8      1     2    0.067960     117.72    31.60         -         -
```

A configuration that cannot run — a database larger than RAM, say — prints its
reason on one line and the sweep carries on to the next.

Two files come out of a sweep: `results.csv` with the rows, and `results.log`
with each run's full output, so a suspicious row can be traced back.

The CSV columns are:

```
threads,d,nb,nq,k,runs,time_s,qps,pkg_j,dram_j
```

`time_s`, `pkg_j` and `dram_j` are per search, averaged over `runs`. The `GB/s`
shown on screen is derived rather than stored: every query scans the whole
database, so it is `nb * (d/8) * nq / time_s`. Watching it flatten as `nb`
grows is how you see the search stop being compute-bound and start being
bound by memory bandwidth.

Each run prints one `CSV,...` line that the script collects, so a single run
can be appended to a sheet by hand the same way.
