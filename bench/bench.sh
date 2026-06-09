#!/usr/bin/env bash
# Build every variant with the SAME compiler (clang) and compare per-packet
# overhead. Laptop numbers are illustrative; publish only from isolated cores.
set -uo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-clang++-18}
LLVM_CFG=${LLVM_CFG:-llvm-config-18}
N=${1:-2000000}
PIN=${PIN:-taskset -c 2}
I="-O2 -std=c++17 -Iinclude -DPATHPROF_TIMING"
E=bench/compare
B=/tmp/ppbench
mkdir -p $B

echo "compiler: $($CXX --version | head -1)"
$CXX -fPIC -shared $($LLVM_CFG --cxxflags) -fno-rtti plugin/PathProfPass.cpp -o $B/PathProfPass.so

$CXX -O2 -std=c++17 -Iinclude                  $E/main_baseline.cpp -o $B/baseline   -lrt
$CXX $I -UPATHPROF_TIMING                       $E/main_raii.cpp     -o $B/raii_count -lrt
$CXX $I                                         $E/main_raii.cpp     -o $B/raii_timed -lrt
$CXX $I -finstrument-functions -rdynamic        $E/main_cyg.cpp      -o $B/cyg_timed  -ldl -lrt
$CXX $I -fxray-instrument -fxray-instruction-threshold=10000 -fno-optimize-sibling-calls \
        -rdynamic                               $E/main_xray.cpp     -o $B/xray_timed -ldl -lrt
$CXX $I -fpass-plugin=$B/PathProfPass.so -rdynamic \
                                                $E/main_plugin.cpp   -o $B/plugin_timed -ldl -lrt

echo; printf '%-14s %10s %10s   %s\n' mode ns/pkt cyc/pkt note
note() { case $1 in
  baseline)   echo "uninstrumented";;
  raii_count) echo "hash+stack only (dozen-cycle target)";;
  raii_timed) echo "+ rdtsc per gate";;
  cyg_timed)  echo "stock -finstrument-functions";;
  xray_timed) echo "XRay sleds + handler";;
  plugin_timed) echo "Clang pass plugin (inlined, attr-gated)";; esac; }
for b in baseline raii_count raii_timed cyg_timed xray_timed plugin_timed; do
    [[ -x $B/$b ]] || { printf '%-14s   (build failed)\n' "$b"; continue; }
    line=$(for r in 1 2 3; do $PIN $B/$b "$N" 2>&1 >/dev/null; done | \
           awk '{for(i=1;i<=NF;i++){if($i ~ /ns\/pkt/)n=$(i-1); if($i ~ /cyc\/pkt/)c=$(i-1)} print n" "c}' | sort -n | head -1)
    printf '%-14s %10s %10s   %s\n' "$b" ${line:-? ?} "$(note $b)"
done