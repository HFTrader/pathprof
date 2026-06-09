// pathprof/report.h - print the top-N paths from a slab. Not hot-path code.
//
// Deliberately STL-free: under the cyg (-finstrument-functions) front-end any
// std::vector / std::sort here would itself be instrumented and pollute the
// slab while we render it. A fixed stack array + selection of the top N keeps
// report() fully self-contained (it only calls printf).
#pragma once
#include "core.h"
#include <cstdio>

namespace pathprof {

PP_RT inline void report(const Slab& s, int topN) {
    const PathEntry* top[PP_MAX_PATHS];
    int n = 0;
    for (const auto& e : s.paths) if (e.count) top[n++] = &e;

    printf("\n=== pathprof: top %d paths ===\n", topN);
    printf("events=%llu  path_records=%llu  unique_paths=%d  dropped=%llu\n",
           (unsigned long long)s.total_events, (unsigned long long)s.path_records,
           n, (unsigned long long)s.dropped);

    int show = topN < n ? topN : n;
    for (int i = 0; i < show; i++) {
        int best = i;                                   // selection sort by count
        for (int j = i + 1; j < n; j++)
            if (top[j]->count > top[best]->count) best = j;
        const PathEntry* tmp = top[i]; top[i] = top[best]; top[best] = tmp;

        const PathEntry* e = top[i];
        double pct = s.path_records ? 100.0 * e->count / s.path_records : 0.0;
        printf("\n#%d  count=%llu (%.1f%%)  hash=%016llx\n", i + 1,
               (unsigned long long)e->count, pct, (unsigned long long)e->hash);
        printf("    path: ");
        for (uint32_t k = 0; k < e->depth; k++)
            printf("%s%s", k ? " > " : "", e->name[k]);
        printf("\n");
#ifdef PATHPROF_TIMING
        for (uint32_t k = 0; k < e->depth; k++) {
            const Hist& h = e->seg[k];
            printf("      [%u] %-16s n=%-8llu min=%-5llu p50=%-6llu p90=%-7llu p99=%-7llu max=%-7llu (cyc)\n",
                   k, e->name[k], (unsigned long long)h.count, (unsigned long long)h.min,
                   (unsigned long long)h.pct(.50), (unsigned long long)h.pct(.90),
                   (unsigned long long)h.pct(.99), (unsigned long long)h.max);
        }
#endif
    }
    printf("\n");
}

PP_RT inline void summary(int topN) { report(*g_slab, topN); }

} // namespace pathprof
