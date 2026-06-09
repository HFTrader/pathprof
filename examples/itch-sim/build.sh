#!/usr/bin/env bash
# Build the pass plugin, then the sim with it. Clang only (the plugin is a
# Clang/LLVM pass). For GCC or a no-plugin build, use the RAII front-end instead.
set -e
cd "$(dirname "$0")"
ROOT=../..
CXX=${CXX:-clang++-18}
LLVM=${LLVM_CONFIG:-llvm-config-18}

$CXX -fPIC -shared $($LLVM --cxxflags) -fno-rtti $ROOT/plugin/PathProfPass.cpp -o PathProfPass.so
$CXX -O2 -std=c++17 -I$ROOT/include -DPATHPROF_TIMING \
     -fpass-plugin=./PathProfPass.so -rdynamic sim.cpp -o sim -ldl
echo "built ./sim  —  run: ./sim 2000000"
