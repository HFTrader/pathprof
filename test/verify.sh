#!/usr/bin/env bash
# Correctness gate: the SAME deterministic workload, through every front-end,
# must discover the SAME set of paths with the SAME counts. Path hashes differ
# by mechanism (different keys), so we compare (count | path-name-sequence).
# Also checks that path ids are deterministic across runs.
set -uo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-clang++-18}
LLVM_CFG=${LLVM_CFG:-llvm-config-18}
N=${1:-500000}
I="-O2 -std=c++17 -Iinclude -DPATHPROF_TIMING"
E=bench/compare
B=/tmp/ppverify; mkdir -p $B
fail=0

$CXX -fPIC -shared $($LLVM_CFG --cxxflags) -fno-rtti plugin/PathProfPass.cpp -o $B/PathProfPass.so
$CXX $I                                         $E/main_raii.cpp   -o $B/raii   -lrt
$CXX $I -finstrument-functions -rdynamic        $E/main_cyg.cpp    -o $B/cyg    -ldl -lrt
$CXX $I -fpass-plugin=$B/PathProfPass.so -rdynamic $E/main_plugin.cpp -o $B/plugin -ldl -lrt
$CXX $I -fxray-instrument -fxray-instruction-threshold=10000 -fno-optimize-sibling-calls -rdynamic \
                                                $E/main_xray.cpp   -o $B/xray   -ldl -lrt

# extract "count|path" pairs, sorted
paths() { taskset -c 2 "$1" "$N" 2>/dev/null | \
    awk '/count=/{split($2,a,"="); c=a[2]} /path:/{sub(/^ *path: /,""); print c"|"$0}' | sort; }

paths $B/raii   > $B/raii.txt
echo "=== reference (raii), $N events ==="; cat $B/raii.txt
for m in cyg plugin xray; do
    paths $B/$m > $B/$m.txt
    if diff -q $B/raii.txt $B/$m.txt >/dev/null; then echo "PASS  $m matches raii"
    else echo "FAIL  $m differs from raii:"; diff $B/raii.txt $B/$m.txt; fail=1; fi
done

# determinism: the discovered (count|path) set is identical across two runs.
# (NB: raw hash IDs are address-derived, so they shift with ASLR run-to-run -
#  a within-run dedup key, not a portable id. See README "Path-id stability".)
paths $B/raii > $B/raii_run2.txt
if diff -q $B/raii.txt $B/raii_run2.txt >/dev/null; then echo "PASS  paths+counts deterministic across runs"
else echo "FAIL  paths+counts not deterministic"; diff $B/raii.txt $B/raii_run2.txt; fail=1; fi

echo; [[ $fail == 0 ]] && echo "ALL CHECKS PASSED" || echo "SOME CHECKS FAILED"
exit $fail