# pathprof

A low-overhead **call-path profiler** for production C++: it tells you *which
paths through your code actually run hot* and *where the time goes per segment* —
at a cost you can leave switched on. Different from sampling (which finds hot
functions, not hot paths) and from heavyweight instrumentation like Callgrind
(which is orders of magnitude slower).

Built for the HFT / low-latency case: you have an event loop and a handful of
hot paths through a parser / book builder / strategy, and you want a global view
of the real path distribution and per-stage latency from a live process.

## How it works

- A per-thread **running hash** encodes the active call path. At each gate entry
  the function's salt is mixed in; on exit it is restored. Two ops on the hot path.
- A per-thread **shadow stack** records each frame's entry TSC and whether it
  ever called a child.
- On a **leaf return** (a frame that called nobody) the running hash is the id of
  a complete root→leaf path. We fold it into a fixed-size table, bump a counter,
  and add each segment's duration to a per-segment histogram. No allocation on the
  hot path, no offline decode.
- The aggregate lives in a POD slab that can be placed in **shared memory**, so an
  external `pathprof-top` renders the live profile without touching the traced
  process.

This is the *probabilistic calling context* idea (Bond & McKinley, OOPSLA 2007)
extended with per-segment timing and an online top-N path table. See
[docs/THEORY.md](docs/THEORY.md) for the lineage (Ball–Larus, CCT, precise/adaptive
encoding, XRay, `-finstrument-functions`/uftrace, Intel PT).

## The runtime is the product; injection is a menu

The runtime (`enter`/`leave`) is independent of *how* gates get inserted. Four
front-ends, none requiring a forked compiler:

| Front-end | Toolchain | Gating | Inlined | Runtime toggle | Notes |
|-----------|-----------|--------|:-------:|:--------------:|-------|
| **RAII** `PP_GATE("x")` | any C++ | you place the macro | yes | no | most portable; pick gates deliberately |
| **cyg** `-finstrument-functions` | GCC/Clang | per-TU + exclude lists | no | no | auto; function address is a free salt; broad blast radius |
| **XRay** `[[clang::xray_always_instrument]]` | Clang | attribute | no (sled) | **yes** | ship dark, `__xray_patch()` live; sees TAIL events |
| **plugin** `[[clang::annotate("pathprof")]]` | Clang + `.so` | attribute | yes | no | inlined like RAII, no source edits; version-coupled to LLVM |

All four are verified to discover **bit-identical paths and counts** on the same
workload ([test/verify.sh](test/verify.sh)).

## Overhead (Zen5 laptop, core 2, N=2M — illustrative, not publication numbers)

~3.1 gates per event. Run it yourself with [bench/bench.sh](bench/bench.sh).

| mode | cyc/pkt | note |
|------|--------:|------|
| baseline | ~39 | uninstrumented |
| XRay, unpatched | ~39 | sleds are nops when off (zero overhead) |
| RAII, count-only | ~119 | hash + shadow stack, no rdtsc |
| RAII, timed | ~180 | + rdtsc per gate |
| cyg, timed | ~176 | stock `-finstrument-functions` |
| plugin, timed | ~187 | Clang pass plugin (inlined, attribute-gated) |
| XRay, timed | ~316 | patchable sleds + indirect handler |

The **rdtsc is the tax, not the hash**: count-only is ~26 cyc/gate; the rdtsc
pair roughly doubles it. Every per-segment histogram floors at ~20–24 cycles —
that floor *is* back-to-back rdtsc latency, so for sub-100-cycle segments rdtsc
measures itself. (Count-only + sampled timing is the route to the dozen-cycle
*and* timed target.)

## Quick start

```bash
# header-only runtime + RAII example
g++ -O2 -std=c++17 -Iinclude -DPATHPROF_TIMING examples/itch-sim/main_raii.cpp -o sim -lrt
./sim 2000000                 # prints top paths with per-segment histograms

bash bench/bench.sh           # 4-way overhead table (clang)
bash test/verify.sh           # cross-mechanism correctness gate

# live readout from another terminal
PATHPROF_SHM=/pp PATHPROF_LOOPS=200 ./sim 2000000 &   # producer exports to shm
g++ -O2 -std=c++17 -Iinclude -DPATHPROF_TIMING tools/pathprof-top/pathprof-top.cpp -o pathprof-top -lrt
./pathprof-top /pp --watch=500                        # external live view
```

Instrument your own code (RAII):

```cpp
#include "pathprof/raii.h"
#include "pathprof/report.h"

void parse(...) { PP_GATE("parse"); /* ... */ }   // at the top of each hot fn

// event loop:
for (;;) { pathprof::on_root(); parse(next_event()); }
// at shutdown (or on demand):
pathprof::summary(20);
```

## Design notes / honest caveats

- **rdtsc tax** — see above. `-UPATHPROF_TIMING` gives the count-only build.
- **cyg blast radius** — `-finstrument-functions` instruments every function in
  the TU; scope it with exclude lists / `no_instrument_function`, and `reset()`
  after warmup. The measured region must call no other instrumented function.
- **TCO** — at `-O2` each stage tail-calls the next. The RAII/cyg/plugin gates
  run code on exit, which defeats the tail call and preserves nesting for free.
  XRay patches the optimized binary, so recover nesting with
  `-fno-optimize-sibling-calls` (or handle TAIL events).
- **Path-id stability** — keys are addresses (name-literal / function), so the
  raw 64-bit path *hash* is a within-run dedup id; it shifts with ASLR run to run.
  Paths and counts are fully deterministic. Content-based keys would give a
  portable id at the cost of a name registry.
- **Multi-thread** — context is per-thread (`__thread`, initial-exec model). v1
  shares one slab; per-thread slabs merged at read is the scaling path.
- **TLS** — uses GNU `__thread` (not `thread_local`) to avoid the `_ZTW` wrapper
  that the cyg front-end would otherwise instrument into infinite recursion.

## Status

Working: all four front-ends, shared-memory readout, the ITCH example, bench and
the cross-mechanism correctness gate. Numbers here are from a non-isolated laptop;
treat them as qualitative. Not yet: HdrHistogram-grade buckets, count+sampled-time
mode, per-thread slab merge, a packaged CMake config export.

## License

MIT. See [LICENSE](LICENSE).
