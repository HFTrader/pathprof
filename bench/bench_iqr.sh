#!/usr/bin/env bash
# Publication bench: many reps on an isolated core, report median + IQR of
# cyc/pkt per variant. On aorus: PIN="taskset -c 18" (isolcpus 18-23, Zen2).
set -uo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-clang++-18}
LLVM_CFG=${LLVM_CFG:-llvm-config-18}
N=${1:-5000000}
REPS=${REPS:-21}
PIN=${PIN:-taskset -c 18}
I="-O2 -std=c++17 -Iinclude -DPATHPROF_TIMING"
E=bench/compare
B=/tmp/ppbench; mkdir -p $B

echo "host=$(hostname)  cpu=$(lscpu | awk -F: '/Model name/{gsub(/^ +/,"",$2);print $2; exit}')"
echo "compiler=$($CXX --version | head -1)  N=$N  reps=$REPS  pin='$PIN'"
$CXX -fPIC -shared $($LLVM_CFG --cxxflags) -fno-rtti plugin/PathProfPass.cpp -o $B/PathProfPass.so
$CXX -O2 -std=c++17 -Iinclude              $E/main_baseline.cpp -o $B/baseline   -lrt
$CXX $I -UPATHPROF_TIMING                   $E/main_raii.cpp     -o $B/raii_count -lrt
$CXX $I                                     $E/main_raii.cpp     -o $B/raii_timed -lrt
$CXX $I -finstrument-functions -rdynamic    $E/main_cyg.cpp      -o $B/cyg_timed  -ldl -lrt
$CXX $I -fxray-instrument -fxray-instruction-threshold=10000 -fno-optimize-sibling-calls -rdynamic \
                                            $E/main_xray.cpp     -o $B/xray_timed -ldl -lrt
$CXX $I -fpass-plugin=$B/PathProfPass.so -rdynamic \
                                            $E/main_plugin.cpp   -o $B/plugin_timed -ldl -lrt

echo; printf '%-13s %9s %9s %9s %9s   %s\n' mode p25 median p75 IQR note
note() { case $1 in
  baseline)     echo "uninstrumented";;
  raii_count)   echo "hash+stack only";;
  raii_timed)   echo "+ rdtsc per gate";;
  cyg_timed)    echo "-finstrument-functions";;
  xray_timed)   echo "XRay sled + handler";;
  plugin_timed) echo "Clang pass plugin";; esac; }

for b in baseline raii_count raii_timed cyg_timed xray_timed plugin_timed; do
    [[ -x $B/$b ]] || { printf '%-13s   (missing)\n' "$b"; continue; }
    $PIN $B/$b "$N" >/dev/null 2>&1                       # warmup (discarded)
    vals=$(for r in $(seq 1 $REPS); do
        $PIN $B/$b "$N" 2>&1 >/dev/null | \
          awk '{for(i=1;i<=NF;i++) if($i ~ /cyc\/pkt/) print $(i-1)}'
    done | sort -n)
    read p25 med p75 < <(echo "$vals" | awk '
      function q(p,  idx){idx=int(p*(NR-1))+1; return a[idx]}
      {a[NR]=$1}
      END{printf "%.1f %.1f %.1f\n", q(0.25), q(0.50), q(0.75)}')
    iqr=$(awk -v a=$p25 -v b=$p75 'BEGIN{printf "%.1f", b-a}')
    printf '%-13s %9s %9s %9s %9s   %s\n' "$b" "$p25" "$med" "$p75" "$iqr" "$(note $b)"
done