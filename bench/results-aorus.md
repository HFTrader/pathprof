# Benchmark results — aorus (Zen2)

Publication numbers. Host: `aorus`, AMD Ryzen Threadripper 3960X (Zen2),
isolated cores 18–23 (`isolcpus` + `nohz_full`), pinned `taskset -c 18`.
Compiler: clang 18.1.3. Workload: ITCH sim, N = 2,000,000 packets, ~3.1
instrumented gates per packet. 15 reps after a discarded warmup.
Reproduce: `REPS=15 PIN="taskset -c 18" bash bench/bench_iqr.sh 2000000`.

## Per-packet overhead (cyc/pkt, median [IQR])

| mode                       | median | IQR | Δ vs baseline | note |
|----------------------------|-------:|----:|--------------:|------|
| baseline                   | 101.8  | 0.1 |       —       | uninstrumented |
| XRay, unpatched (nops)     | ~109   |  —  |      ~0       | sleds are nops when off |
| RAII, count-only           | 372.2  | 1.0 |    +270       | hash + shadow stack, no rdtsc |
| RAII, timed                | 473.4  | 1.4 |    +372       | + rdtsc per gate |
| cyg, timed                 | 471.8  | 4.5 |    +370       | stock -finstrument-functions |
| plugin, timed              | 488.7  | 2.5 |    +387       | Clang pass plugin (inlined) |
| XRay-as-injection, timed   | 595.6  | 1.7 |    +494       | our handler on XRay sleds |
| XRay native FDR logging    | ~1720  |  —  |   +1618       | full trace, offline decode |

The IQR of 1–5 cycles on an isolated core says the measurement is solid.

## Reading it

- **Instrumentation cost** (count-only RAII): +270 cyc/pkt over baseline for ~3.1
  hash mixes + shadow-stack pushes plus one path-table lookup and segment update
  on the leaf.
- **The rdtsc is the tax, not the hash**: timed − count = +101 cyc/pkt for the
  rdtsc pair per gate. On Zen2 rdtsc is comparatively expensive; this is the
  single biggest knob. (Count-only is the dozen-cycle-class gate; timing is what
  costs.)
- **Inlined injection is cheapest**: RAII (473), cyg (472), plugin (489) cluster
  together — the gate body inlines or is a cheap local call.
- **Riding XRay's sleds costs +26%** over an inlined gate (595 vs 473): the sled
  is an indirect call with register save/restore around it.
- **XRay's *native* logging is 3.6× our gate** (1720 vs 473) and produces a full
  event trace that must be decoded offline with `llvm-xray` — the opposite of an
  online few-KB path table. This is the core "why not just XRay" answer: XRay is
  an excellent instrumentation *substrate* (we use it as a front-end), but its
  built-in *consumer* is the wrong tool for online path aggregation on a budget.

Laptop (Zen5) numbers in the repo history are ~2–3× lower in absolute cycles but
show the same ordering; treat only these aorus numbers as publication-grade.
