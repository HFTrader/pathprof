// pathprof-top - read a traced process's live path profile from shared memory.
//
//   pathprof-top [/shm-name] [topN] [--watch[=ms]]
//
// The slab is self-describing (segment names are baked in), so this needs no
// symbols from the traced binary. Build with -DPATHPROF_TIMING to show the
// per-segment histograms.
#include "pathprof/shm.h"
#include "pathprof/report.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

int main(int argc, char** argv) {
    const char* name = "/pathprof";
    int topN = 12, watch_ms = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/') name = argv[i];
        else if (!strncmp(argv[i], "--watch", 7))
            watch_ms = (argv[i][7] == '=') ? atoi(argv[i] + 8) : 500;
        else topN = atoi(argv[i]);
    }

    pathprof::Slab* s = pathprof::map_shm(name, false);
    if (!s) { fprintf(stderr, "pathprof-top: cannot open shm '%s' (is the app running with PATHPROF_SHM=%s?)\n", name, name); return 1; }
    if (s->magic != pathprof::SLAB_MAGIC)
        fprintf(stderr, "pathprof-top: warning: magic mismatch (%016llx), layout may differ\n", (unsigned long long)s->magic);

    do {
        if (watch_ms) printf("\033[2J\033[H");          // clear screen
        pathprof::report(*s, topN);
        if (watch_ms) {
            fflush(stdout);
            timespec ts{watch_ms / 1000, (watch_ms % 1000) * 1000000L};
            nanosleep(&ts, nullptr);
        }
    } while (watch_ms);
    return 0;
}
