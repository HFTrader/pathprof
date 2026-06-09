// pathprof/cyg.h - front-end 2: -finstrument-functions (stock GCC & Clang).
//
// Compile the TUs you want traced with -finstrument-functions; the compiler
// injects calls to the two hooks below at every function entry/exit. The gate
// key is the function's own address (this_fn) - a free, stable, unique salt, so
// no registration table is needed. Names resolve via dladdr at first sight, so
// link with -rdynamic -ldl.
//
// Narrow the blast radius with __attribute__((no_instrument_function)) on
// helpers, or -finstrument-functions-exclude-{file,function}-list=.
//
// Note: because the exit hook must run after the body, the compiler cannot
// tail-call out of an instrumented function - so, like the RAII gate, this
// preserves call nesting (unlike XRay, which patches the optimized binary).
#pragma once
#include "core.h"
#include "resolve.h"

namespace pathprof { inline const int _pp_cyg_autoinit = (use_addr_resolver(), 0); }

extern "C" {
__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* this_fn, void* /*call_site*/) {
    ::pathprof::enter((uint64_t)this_fn);
}
__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void* /*this_fn*/, void* /*call_site*/) {
    ::pathprof::leave();
}
}
