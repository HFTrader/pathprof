// pathprof/resolve.h - turn a function address into a name via dladdr.
// Used by the address-keyed front-ends (cyg, xray). Link with -ldl -rdynamic.
#pragma once
#include "core.h"
#include <dlfcn.h>
#include <cxxabi.h>
#include <cstdlib>
#include <cstring>

namespace pathprof {

// Demangle into a small thread-local buffer (called only on first-sight).
PP_RT inline const char* resolve_addr(uint64_t fn_key) {
    static thread_local char buf[256];
    Dl_info info;
    if (!dladdr((void*)fn_key, &info) || !info.dli_sname) {
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)fn_key);
        return buf;
    }
    int status = 0;
    char* dem = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
    const char* src = (status == 0 && dem) ? dem : info.dli_sname;
    // drop the parameter list for readability ("book_add(...)" -> "book_add").
    size_t n = 0;
    while (src[n] && src[n] != '(' && n < sizeof(buf) - 1) { buf[n] = src[n]; n++; }
    buf[n] = 0;
    if (dem) std::free(dem);
    return buf;
}

inline void use_addr_resolver() { g_resolve = resolve_addr; }

} // namespace pathprof
