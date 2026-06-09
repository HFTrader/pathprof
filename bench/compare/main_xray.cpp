// XRay-instrumented ITCH sim (rides XRay's sleds; our handler does the profiling).
// Build: clang++ -O2 -fxray-instrument -fno-optimize-sibling-calls -rdynamic main_xray.cpp -ldl
#define PP_USE_XRAY
#include "pipeline.h"
[[clang::xray_never_instrument]]
int main(int argc, char** argv) {
    pathprof::xray_start();              // install handler + arm the sleds
    int rc = run(argc, argv, "xray");
    pathprof::xray_stop();
    return rc;
}
