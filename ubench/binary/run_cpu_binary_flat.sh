#!/bin/bash
# Sweeps d / nb / nq / k over IndexBinaryFlat.
# Override any axis from the environment, e.g.
#   D_LIST="256 512" NB_LIST=4194304 ./run_cpu_binary_flat.sh

set -u

PROGRAM="./cpu_binary_flat"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: '$PROGRAM' not found or not executable. Run 'make' first."
    exit 1
fi

D_LIST="${D_LIST:-64 128 256 512}"
NB_LIST="${NB_LIST:-1048576 4194304 16777216}"
NQ_LIST="${NQ_LIST:-1 16 128}"
K_LIST="${K_LIST:-1 16 256}"
RUNS="${RUNS:-5}"
HEAP="${HEAP:-1}"

for D in $D_LIST; do
    for NB in $NB_LIST; do
        echo "============================================="
        echo "d=$D bits | nb=$NB"
        echo "============================================="
        for NQ in $NQ_LIST; do
            for K in $K_LIST; do
                echo "--- d=$D nb=$NB nq=$NQ k=$K heap=$HEAP ---"
                "$PROGRAM" -d "$D" -nb "$NB" -nq "$NQ" -k "$K" -r "$RUNS" -heap "$HEAP"
                echo
            done
        done
    done
done
