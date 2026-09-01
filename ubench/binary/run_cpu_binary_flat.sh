#!/bin/bash
# Sweeps threads / d / nb / nq / k over IndexBinaryFlat, printing each result as
# it lands and collecting them into a CSV. Counts accept a binary K/M/G suffix.
# Override any axis from the environment:
#   OUT=big.csv T_LIST="1 8" NB_LIST="1G" RUNS=10 ./run_cpu_binary_flat.sh

PROGRAM="./cpu_binary_flat"

if [ ! -x "$PROGRAM" ]; then
    echo "Error: '$PROGRAM' not found. Run 'make' first."
    exit 1
fi

OUT="${OUT:-results.csv}"
LOG="${OUT%.csv}.log"
T_LIST="${T_LIST:-1 2 4 8}"
D_LIST="${D_LIST:-64 128 256 512}"
NB_LIST="${NB_LIST:-1M 4M}"
NQ_LIST="${NQ_LIST:-1 16 128}"
K_LIST="${K_LIST:-1 16 256}"
RUNS="${RUNS:-1}"

echo "threads,d,nb,nq,k,runs,time_s,qps,pkg_j,dram_j" > "$OUT"
: > "$LOG"

# GB/s is derived from the row, not measured separately: every query scans the
# whole database, so nb * (d/8) * nq bytes move per search.
header() {
    printf "%7s %5s %13s %5s %6s %5s %11s %10s %8s %9s %9s\n" \
        threads d nb nq k runs time_s qps GB/s pkg_J dram_J
    printf -- "----------------------------------------------------------------------------------------------\n"
}

header

for T in $T_LIST; do
    for D in $D_LIST; do
        for NB in $NB_LIST; do
            for NQ in $NQ_LIST; do
                for K in $K_LIST; do
                    OUTPUT=$($PROGRAM -t "$T" -d "$D" -nb "$NB" -nq "$NQ" -k "$K" -r "$RUNS")
                    {
                        echo "=== t=$T d=$D nb=$NB nq=$NQ k=$K r=$RUNS ==="
                        echo "$OUTPUT"
                        echo
                    } >> "$LOG"

                    ROW=$(echo "$OUTPUT" | grep '^CSV,' | cut -d, -f2-)
                    if [ -z "$ROW" ]; then
                        # No result line: the run refused or died. Show why.
                        printf "  t=%s d=%s nb=%s nq=%s k=%s -> %s\n" \
                            "$T" "$D" "$NB" "$NQ" "$K" \
                            "$(echo "$OUTPUT" | grep -m1 -i error)"
                        continue
                    fi

                    echo "$ROW" >> "$OUT"
                    echo "$ROW" | awk -F, '{
                        gbs = $3 * ($2 / 8) * $4 / $7 / 1e9
                        printf "%7s %5s %13s %5s %6s %5s %11s %10s %8.2f %9s %9s\n",
                               $1, $2, $3, $4, $5, $6, $7, $8, gbs,
                               ($9 == "" ? "-" : $9), ($10 == "" ? "-" : $10)
                    }'
                done
            done
        done
    done
done

echo
echo "Wrote $OUT ($(($(wc -l < "$OUT") - 1)) rows) and $LOG"
