#!/bin/bash
# Sweeps threads / d / nb / nq / k over IndexBinaryFlat and writes a CSV.
# Counts accept a binary K/M/G suffix. Override any axis from the environment:
#   OUT=big.csv T_LIST="1 8" NB_LIST="1G" RUNS=10 ./run_cpu_binary_flat.sh

PROGRAM="./cpu_binary_flat"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: '$PROGRAM' not found. Run 'make' first."
    exit 1
fi

OUT="${OUT:-results.csv}"
T_LIST="${T_LIST:-1 2 4 8}"
D_LIST="${D_LIST:-64 128 256 512}"
NB_LIST="${NB_LIST:-1M 4M}"
NQ_LIST="${NQ_LIST:-1 16 128}"
K_LIST="${K_LIST:-1 16 256}"
RUNS="${RUNS:-1}"

echo "threads,d,nb,nq,k,runs,time_s,qps,pkg_j,dram_j" > "$OUT"

for T in $T_LIST; do
    for D in $D_LIST; do
        for NB in $NB_LIST; do
            for NQ in $NQ_LIST; do
                for K in $K_LIST; do
                    echo "t=$T d=$D nb=$NB nq=$NQ k=$K r=$RUNS"
                    $PROGRAM -t $T -d $D -nb $NB -nq $NQ -k $K -r $RUNS \
                        | grep '^CSV,' | cut -d, -f2- >> "$OUT"
                done
            done
        done
    done
done

echo
echo "Wrote $OUT ($(($(wc -l < "$OUT") - 1)) rows)"
