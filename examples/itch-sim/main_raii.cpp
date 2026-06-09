// RAII-instrumented ITCH sim. Build with -DPATHPROF_TIMING for per-segment histos.
#define PP_USE_RAII
#include "itch_pipeline.h"
int main(int argc, char** argv) { return run(argc, argv, "raii"); }
