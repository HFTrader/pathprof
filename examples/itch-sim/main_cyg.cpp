// -finstrument-functions-instrumented ITCH sim.
// Build: g++ -O2 -finstrument-functions -rdynamic main_cyg.cpp -ldl
#define PP_USE_CYG
#include "itch_pipeline.h"
__attribute__((no_instrument_function))
int main(int argc, char** argv) { return run(argc, argv, "cyg"); }
