// pathprof/plugin_rt.h - runtime side of front-end 4 (the Clang pass plugin).
//
// The PathProf pass (plugin/PathProfPass.cpp) injects, into every function
// annotated [[clang::annotate("pathprof")]], a call to __pathprof_enter(addr)
// at entry and __pathprof_leave() before each return. The key is the function's
// own address, so names resolve via dladdr (link -rdynamic -ldl). Because the
// leave() call sits after the body, the tail call is defeated and nesting is
// preserved - no -fno-optimize-sibling-calls needed (unlike the XRay sleds).
#pragma once
#include "core.h"
#include "resolve.h"

namespace pathprof { inline const int _pp_plugin_autoinit = (use_addr_resolver(), 0); }

extern "C" PP_RT void __pathprof_enter(uint64_t key) { ::pathprof::enter(key); }
extern "C" PP_RT void __pathprof_leave()             { ::pathprof::leave(); }
