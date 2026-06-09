// pathprof/raii.h - front-end 1: source-level RAII gate.
//
//   PP_GATE("name");   // at the top of any function you want on the path
//
// Most portable mechanism: any ISO C++ compiler, no toolchain support, the
// gate body inlines. The gate key is the address of the name literal, so the
// resolver is a plain cast (no registry, no per-site static guard). Gate names
// must be unique per function (identical literals may be merged by the linker).
#pragma once
#include "core.h"

namespace pathprof {
PP_RT inline const char* resolve_literal(uint64_t k) { return (const char*)k; }
inline void use_raii() { g_resolve = resolve_literal; }
inline const int _pp_raii_autoinit = (use_raii(), 0);   // auto-install resolver
}

#define PP_CAT2(a, b) a##b
#define PP_CAT(a, b) PP_CAT2(a, b)
// "" name forces a string literal (rejects non-literal args).
#define PP_GATE(name) \
    ::pathprof::Gate PP_CAT(_pp_gate_, __LINE__)((uint64_t)(const void*)("" name))
