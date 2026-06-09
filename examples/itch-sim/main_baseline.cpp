// Uninstrumented baseline (same pipeline, no gates) for overhead comparison.
#include "itch_pipeline.h"
int main(int argc, char** argv) { return run(argc, argv, "baseline"); }
