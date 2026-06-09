# Theory and prior art

`pathprof` is an engineering assembly of well-studied ideas. This is the lineage,
with primary sources, and where our design sits relative to each.

## Path encoding

- **Ball–Larus efficient path profiling** (PLDI 1996). A single register
  accumulates an integer that uniquely identifies an *acyclic intra-procedural*
  path, via edge weights on a DAG. The intra-procedural ancestor of our running
  hash. LLVM once shipped this (`-insert-path-profiling` / `llvm-prof`); it was
  removed around LLVM 3.5 as unmaintained. Modern reimplementation: lac-dcc/Nisse.

- **Calling Context Tree** (Ammons, Ball, Larus, PLDI 1997). The data structure
  most context-sensitive profilers build; XRay's profiling mode builds one per
  thread (`FunctionCallTrie`). We keep a flat top-N table keyed by a path hash
  instead of the full tree.

- **Probabilistic Calling Context** (Bond & McKinley, OOPSLA 2007). Maintains a
  thread-local running value `V = 3V + callsite_id` that is a probabilistically
  unique hash of the entire active call path, ~3% overhead, no stack walking.
  This is exactly our running hash. Its known limitation — you cannot decode the
  hash back to a path — we sidestep by snapshotting the function-name sequence
  into the slab on first sight.

- **Precise / Adaptive Calling Context Encoding** (Sumner et al., ICSE 2010; Li et
  al., CGO 2014). Lossless, decodable context ids using the static call graph.
  The route to portable, collision-free path ids if the within-run hash is not
  enough.

## Injection mechanisms

- **`-finstrument-functions`** (`__cyg_profile_func_enter/exit`). Stock GCC/Clang;
  the basis of **uftrace**. Our cyg front-end.
- **LLVM XRay** (`-fxray-instrument`, `[[clang::xray_always_instrument]]`).
  Patchable entry/exit sleds, nop when unpatched; custom handler via
  `__xray_set_handler` + `__xray_patch`. Our xray front-end rides the sleds.
- **Clang pass plugin** (new PM, `-fpass-plugin`). Reads `[[clang::annotate]]`
  and injects calls at IR level. Our plugin front-end. No forked compiler — a
  loadable `.so`.

## Alternatives we are not

- **Sampling profilers** (perf, gperftools, HPCToolkit). Find hot functions /
  reconstruct CCTs by async unwinding; cannot see every path's exact count cheaply.
- **Intel Processor Trace** (magic-trace, `perf intel_pt`, libipt). Exact control
  flow at low runtime cost, but offline decode and large traces — the opposite
  end (hardware, post-hoc) from our online aggregation.
- **HPC instrumentation** (TAU callpath profiling, Score-P/Scalasca). Per-callpath
  inclusive/exclusive timing online, but built for MPI clusters, not a
  dozen-cycle budget.

## Where pathprof sits

The novel-in-combination part is not the encoding or the injection — both are
solved — but the **online top-N path table with per-segment histograms cheap
enough to leave on in production, plus live shared-memory readout**. The corner
the literature mostly left open.
