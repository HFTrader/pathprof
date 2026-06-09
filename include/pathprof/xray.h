// pathprof/xray.h - front-end 3: ride LLVM XRay's instrumentation (stock Clang).
//
// Annotate hot functions with [[clang::xray_always_instrument]] and build with
// -fxray-instrument. XRay lays down patchable entry/exit sleds; we install our
// own handler instead of XRay's logging, so the sleds feed the path profiler.
//
//   pathprof::xray_start();   // install handler + patch the sleds (arm)
//   ... run ...
//   pathprof::xray_stop();    // unpatch (sleds revert to nops)
//
// The killer property: before xray_start() the sleds are nops (zero overhead) -
// ship the binary dark and arm it live in production. The gate key is the XRay
// funcId; names resolve via __xray_function_address + dladdr (link -rdynamic -ldl).
//
// TCO caveat: XRay patches the *optimized* binary, so at -O2 each stage
// tail-calls the next and the handler sees TAIL events (we pop without
// recording, so the callee replaces the caller - paths flatten, matching the
// real control flow). Build with -fno-optimize-sibling-calls to recover the
// nested call paths the RAII/cyg front-ends give for free.
#pragma once
#include "core.h"
#include "resolve.h"
#include <xray/xray_interface.h>

namespace pathprof {

PP_RT inline void xray_handler(int32_t fid, XRayEntryType t) {
    switch (t) {
        case ENTRY: case LOG_ARGS_ENTRY: enter((uint64_t)(uint32_t)fid); break;
        case EXIT:                       leave(false); break;
        case TAIL:                       leave(true);  break;   // callee replaces us
        default: break;
    }
}

PP_RT inline const char* resolve_xray(uint64_t fid) {
    uintptr_t a = __xray_function_address((int32_t)fid);
    return a ? resolve_addr(a) : "?";
}

PP_RT inline void xray_start() {
    g_resolve = resolve_xray;
    __xray_set_handler(xray_handler);
    __xray_patch();
}
PP_RT inline void xray_stop() {
    __xray_unpatch();
    __xray_remove_handler();
}

} // namespace pathprof
