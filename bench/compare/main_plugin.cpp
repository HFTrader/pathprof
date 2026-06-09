// Pass-plugin-instrumented ITCH sim. Functions tagged [[clang::annotate("pathprof")]]
// are gated by the PathProf pass at compile time.
// Build: clang++ -O2 -fpass-plugin=./PathProfPass.so -rdynamic main_plugin.cpp -ldl
#define PP_USE_PLUGIN
#include "pipeline.h"
int main(int argc, char** argv) { return run(argc, argv, "plugin"); }
