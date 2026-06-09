// pathprof/annotate.h - the attribute front-end. Tag the functions you want on
// the path with PATHPROF and build with the PathProf pass plugin:
//
//   PATHPROF void book_add(...) { ... }
//
//   clang++ -O2 -fpass-plugin=PathProfPass.so -rdynamic app.cpp -ldl
//
// The pass injects the gate only into annotated functions, so there is nothing
// else to configure: no exclude lists, no per-call macros, no reset. noinline
// keeps each annotated function a real call boundary for the pass to find.
#pragma once
#include "pathprof/plugin_rt.h"   // __pathprof_enter/leave + dladdr naming
#include "pathprof/report.h"      // pathprof::summary

#define PATHPROF [[clang::annotate("pathprof")]] __attribute__((noinline))
