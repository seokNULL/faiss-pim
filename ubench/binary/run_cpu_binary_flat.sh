#!/bin/bash
# Sweeps threads / d / nb / nq / k over IndexBinaryFlat.
# Override any axis from the environment, e.g.
#   T_LIST="1 8" D_LIST=256 NB_LIST=4194304 ./run_cpu_binary_flat.sh

PROGRAM="./cpu_binary_flat"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: '$PROGRAM' not found. Run 'make' first."
    exit 1
fi

T_LIST="${T_LIST:-1 2 4 8}"
D_LIST="${D_LIST:-64 128 256 512}"
NB_LIST="${NB_LIST:-1048576 4194304}"
NQ_LIST="${NQ_LIST:-1 16 128}"
K_LIST="${K_LIST:-1 16 256}"

for T in $T_LIST; do
    for D in $D_LIST; do
        for NB in $NB_LIST; do
            for NQ in $NQ_LIST; do
                for K in $K_LIST; do
                    echo "=== t=$T d=$D nb=$NB nq=$NQ k=$K ==="
                    $PROGRAM -t $T -d $D -nb $NB -nq $NQ -k $K | grep -E "Query Time|QPS"
                    echo
                done
            done
        done
    done
done
