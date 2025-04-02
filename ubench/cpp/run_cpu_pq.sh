#!/bin/bash

# Path to the compiled program
PROGRAM="./cpu_pq"

# Check if the program exists
if [ ! -f "$PROGRAM" ]; then
    echo "Error: Program '$PROGRAM' not found."
    exit 1
fi

# Different database sizes (in number of vectors)
for NB in 1048576 2097152 4194304 8388608; do  # 512MB, 1GB, 2GB, 4GB
    echo "============================================="
    echo "Running experiments with NB=$NB"
    echo "============================================="
    
    NQ=1  # Start NQ at 1
    while [ $NQ -le 256 ]; do
        K=1  # Start K at 1
        while [ $K -le 256 ]; do
            echo "[NB=$NB] >>> NQ=$NQ | K=$K <<<"
            $PROGRAM -nb $NB -nq $NQ -k $K
            K=$((K * 4))  # Increase K by 4x
        done
        NQ=$((NQ * 2))  # Increase NQ by 4x
    done
done
