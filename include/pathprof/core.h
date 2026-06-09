// pathprof/core.h - mechanism-agnostic path-profiler runtime (header-only).
//
// A per-thread RUNNING HASH encodes the active call path; a per-thread SHADOW
// STACK tracks each frame's entry TSC and whether it ever called a child. On a
// LEAF return (a frame that called nobody) the running hash is the id of a
// complete root->leaf path; we fold it into a fixed-size table with per-segment
// histograms. No allocation on the hot path. No offline decode.
//
// The runtime is independent of HOW gates get injected. Every front-end
// (RAII macro, -finstrument-functions, XRay handler, Clang plugin) is a thin
// shim that calls enter()/leave(). A "gate key" is any 64-bit value the
// mechanism has cheaply: a name-literal pointer, a function address, an XRay
// funcId. We splitmix it into a salt, so even small or clustered keys spread.
//
// Compile knobs:
//   PATHPROF_TIMING   read rdtsc at each gate; keep per-segment histograms.
//   PP_MAX_PATHS / PP_MAX_SEG / PP_MAX_DEPTH / PP_NAMELEN   table bounds.
#pragma once
#include <cstdint>
#include <cstring>
#include <x86intrin.h>

#ifndef PP_MAX_DEPTH
#define PP_MAX_DEPTH 64
#endif
#ifndef PP_MAX_SEG
#define PP_MAX_SEG 24          // longest path recorded (segments)
#endif
#ifndef PP_MAX_PATHS
#define PP_MAX_PATHS 1024      // power of two; table never resizes
#endif
#ifndef PP_NAMELEN
#define PP_NAMELEN 28          // per-segment name snapshot (self-describing slab)
#endif
#define PP_HBUCKETS 40         // log2 cycle histogram

// Every runtime function carries this so no injection mechanism ever re-enters
// the runtime through its own hooks: no_instrument_function bars the cyg
// front-end, xray_never_instrument bars XRay (whose default threshold would
// otherwise sled record_leaf et al.). Harmless under RAII/plugin.
#if defined(__clang__)
  #define PP_RT __attribute__((no_instrument_function, xray_never_instrument))
#else
  #define PP_RT __attribute__((no_instrument_function))
#endif

namespace pathprof {

PP_RT inline uint64_t rdtsc() { return __rdtsc(); }   // non-serializing; see docs

constexpr uint64_t SALT  = 0xCBF29CE484222325ull;   // FNV-1a offset basis
constexpr uint64_t PRIME = 0x100000001B3ull;        // FNV-1a prime

PP_RT inline uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
// Order-sensitive mix: A>B differs from B>A. Two ops on the hot path.
PP_RT inline uint64_t mix_hash(uint64_t h, uint64_t salt) { return (h ^ salt) * PRIME; }

// ---------------------------------------------------------------------------
// compact log2-bucket histogram (POD; lives in the shared slab)
// ---------------------------------------------------------------------------
struct Hist {
    uint32_t bucket[PP_HBUCKETS];
    uint64_t count, sum, min, max;
    PP_RT void add(uint64_t v) {
        count++; sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
        int b = v ? 63 - __builtin_clzll(v) : 0;
        if (b >= PP_HBUCKETS) b = PP_HBUCKETS - 1;
        bucket[b]++;
    }
    PP_RT uint64_t pct(double p) const {
        if (!count) return 0;
        uint64_t target = (uint64_t)(count * p), acc = 0;
        for (int b = 0; b < PP_HBUCKETS; b++) {
            acc += bucket[b];
            if (acc >= target) return (1ull << b) + (1ull << b) / 2;  // bucket midpoint
        }
        return max;
    }
};

// ---------------------------------------------------------------------------
// the aggregate: one POD struct, placeable in shared memory so an external
// reader (pathprof-top) can render it live without touching the traced process.
// Self-describing: segment names are snapshotted on first sight.
// ---------------------------------------------------------------------------
struct PathEntry {
    uint64_t hash;                       // path id; 0 == empty slot
    uint64_t count;
    uint32_t depth;
    char     name[PP_MAX_SEG][PP_NAMELEN];
    Hist     seg[PP_MAX_SEG];
};
struct Slab {
    uint64_t  magic;                     // for the external reader to validate
    uint64_t  total_events;              // root resets
    uint64_t  path_records;              // leaf records (>= events; replace -> 2)
    uint64_t  dropped;                   // table-full drops
    PathEntry paths[PP_MAX_PATHS];
};

inline Slab  g_static_slab;              // default in-process slab
inline Slab* g_slab = &g_static_slab;    // repoint at mmap'd shm to export live

// name resolver: fn_key -> human name. RAII installs a cast-to-char*; the
// address-based front-ends install a dladdr resolver (see resolve.h). Called
// only on first sight of a path (slow path), never on the hot path.
using Resolver = const char* (*)(uint64_t fn_key);
PP_RT inline const char* default_resolve(uint64_t) { return "?"; }
inline Resolver g_resolve = default_resolve;

PP_RT inline PathEntry* find_or_insert(uint64_t hash) {
    uint32_t idx = (uint32_t)hash & (PP_MAX_PATHS - 1);
    for (int probe = 0; probe < PP_MAX_PATHS; probe++) {
        PathEntry& e = g_slab->paths[idx];
        if (e.hash == hash) return &e;
        if (e.hash == 0) { e.hash = hash; return &e; }   // claim (first-sight hiccup)
        idx = (idx + 1) & (PP_MAX_PATHS - 1);
    }
    return nullptr;                                       // table full
}

// ---------------------------------------------------------------------------
// per-thread context
// ---------------------------------------------------------------------------
struct Frame {
    uint64_t enter_tsc;
    uint64_t hash_before;     // running hash before this fn (for restore)
    uint64_t hash_after;      // running hash after  (== path id if this is the leaf)
    uint64_t fn_key;
    bool     is_leaf;
};
struct Ctx {
    uint64_t running_hash;       // zero-init; salted in on_root()
    int      depth;
    Frame    stack[PP_MAX_DEPTH];
};
// GNU __thread (not C++ thread_local) on purpose: no init guard and no _ZTW
// TLS-wrapper function. The wrapper is emitted for every `inline thread_local`
// and the -finstrument-functions front-end would instrument it into infinite
// recursion (enter() touches the context). __thread behind a PP_RT accessor is
// header-only safe (single instance via the inline fn) and gives a direct
// %fs-relative access - the fastest TLS model, which HFT wants anyway.
PP_RT __attribute__((always_inline)) inline Ctx& ctx() {
    static __thread Ctx t __attribute__((tls_model("initial-exec")));
    return t;
}

// The event loop calls this once per event: re-salt the hash, reset the stack.
PP_RT inline void on_root() {
    Ctx& c = ctx();
    c.running_hash = SALT;
    c.depth = 0;
    g_slab->total_events++;
}

// Wipe the slab and the calling thread's stack. Call after warmup/init so
// startup noise (esp. with the auto-instrumenting cyg front-end) is excluded.
PP_RT inline void reset() {
    std::memset(g_slab->paths, 0, sizeof(g_slab->paths));
    g_slab->total_events = g_slab->path_records = g_slab->dropped = 0;
    Ctx& c = ctx();
    c.depth = 0; c.running_hash = SALT;
}

PP_RT inline void record_leaf(Ctx& c, int leaf_d, uint64_t exit_tsc) {
    uint64_t path_hash = c.stack[leaf_d].hash_after;
    if (path_hash == 0) path_hash = 1;                   // reserve 0 for "empty"
    PathEntry* e = find_or_insert(path_hash);
    if (!e) { g_slab->dropped++; return; }
    if (e->count == 0) {                                 // first sight: snapshot
        e->depth = (leaf_d + 1 > PP_MAX_SEG) ? PP_MAX_SEG : leaf_d + 1;
        for (uint32_t i = 0; i < e->depth; i++) {
            const char* n = g_resolve(c.stack[i].fn_key);
            std::strncpy(e->name[i], n ? n : "?", PP_NAMELEN - 1);
            e->name[i][PP_NAMELEN - 1] = 0;
            e->seg[i].min = ~0ull;
        }
    }
    e->count++;
    g_slab->path_records++;
#ifdef PATHPROF_TIMING
    // segment i (i<leaf): entry -> entry of child i+1 (work before delegating).
    // leaf segment      : entry -> exit.
    uint32_t segs = e->depth;
    for (uint32_t i = 0; i + 1 < segs; i++)
        e->seg[i].add(c.stack[i + 1].enter_tsc - c.stack[i].enter_tsc);
    e->seg[segs - 1].add(exit_tsc - c.stack[segs - 1].enter_tsc);
#else
    (void)exit_tsc;
#endif
}

// ---------------------------------------------------------------------------
// the two hot-path primitives every front-end calls
// ---------------------------------------------------------------------------
PP_RT __attribute__((always_inline)) inline void enter(uint64_t fn_key) {
    Ctx& c = ctx();
    int d = c.depth;
    if (d >= PP_MAX_DEPTH) { c.depth = d + 1; return; }   // overflow guard
    Frame& f = c.stack[d];
    f.hash_before  = c.running_hash;
    c.running_hash = mix_hash(c.running_hash, splitmix(fn_key));
    f.hash_after   = c.running_hash;
    f.fn_key       = fn_key;
    f.is_leaf      = true;
    if (d > 0) c.stack[d - 1].is_leaf = false;            // our parent called a child
#ifdef PATHPROF_TIMING
    f.enter_tsc = rdtsc();
#endif
    c.depth = d + 1;
}

// tail == true: this frame is tail-calling (XRay TAIL). Pop without recording,
// so the tail-callee replaces us in the path (matches the optimized control flow).
PP_RT __attribute__((always_inline)) inline void leave(bool tail = false) {
#ifdef PATHPROF_TIMING
    uint64_t now = rdtsc();
#else
    uint64_t now = 0;
#endif
    Ctx& c = ctx();
    int d = --c.depth;
    if (d >= PP_MAX_DEPTH || d < 0) return;               // matched overflow guard
    Frame& f = c.stack[d];
    if (f.is_leaf && !tail) record_leaf(c, d, now);
    c.running_hash = f.hash_before;                       // restore parent's hash
}

// RAII convenience over enter()/leave().
struct Gate {
    PP_RT __attribute__((always_inline)) Gate(uint64_t fn_key) { enter(fn_key); }
    PP_RT __attribute__((always_inline)) ~Gate() { leave(); }
};

} // namespace pathprof
