// itch_pipeline.h - a toy market-data pipeline, instrumentable by any front-end.
//
//   fake socket -> event loop -> packet_parse -> itch_parse
//                                  -> book_{add,delete,cancel,execute,replace} -> consumer
//
// Select a mechanism by defining ONE of PP_USE_{RAII,CYG,XRAY,PLUGIN} before
// including (none => uninstrumented baseline). The pipeline functions are
// written once; the hooks below expand per mechanism.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <x86intrin.h>

// ---- per-mechanism hooks ---------------------------------------------------
#if defined(PP_USE_RAII)
  #include "pathprof/raii.h"
  #include "pathprof/report.h"
  #define PP_FN       __attribute__((noinline))
  #define PP_BODY(n)  PP_GATE(n)
  #define PP_NOINSTR
  #define PP_ROOT()   ::pathprof::on_root()
  #define PP_RESET()  ::pathprof::reset()
  #define PP_SUMMARY(k) ::pathprof::summary(k)
#elif defined(PP_USE_CYG)
  #include "pathprof/cyg.h"
  #include "pathprof/report.h"
  #define PP_FN       __attribute__((noinline))
  #define PP_BODY(n)
  #define PP_NOINSTR  __attribute__((no_instrument_function))
  #define PP_ROOT()   ::pathprof::on_root()
  #define PP_RESET()  ::pathprof::reset()
  #define PP_SUMMARY(k) ::pathprof::summary(k)
#elif defined(PP_USE_XRAY)
  #include "pathprof/xray.h"
  #include "pathprof/report.h"
  #define PP_FN       [[clang::xray_always_instrument]] __attribute__((noinline))
  #define PP_BODY(n)
  #define PP_NOINSTR  __attribute__((xray_never_instrument))
  #define PP_ROOT()   ::pathprof::on_root()
  #define PP_RESET()  ::pathprof::reset()
  #define PP_SUMMARY(k) ::pathprof::summary(k)
#elif defined(PP_USE_PLUGIN)
  #include "pathprof/plugin_rt.h"
  #include "pathprof/report.h"
  #define PP_FN       [[clang::annotate("pathprof")]] __attribute__((noinline))
  #define PP_BODY(n)
  #define PP_NOINSTR
  #define PP_ROOT()   ::pathprof::on_root()
  #define PP_RESET()  ::pathprof::reset()
  #define PP_SUMMARY(k) ::pathprof::summary(k)
#else  // baseline
  #define PP_FN       __attribute__((noinline))
  #define PP_BODY(n)
  #define PP_NOINSTR
  #define PP_ROOT()
  #define PP_RESET()
  #define PP_SUMMARY(k)
#endif

// Optional shared-memory export: set PATHPROF_SHM=/name to let pathprof-top read
// this process's live path profile from outside.
#if defined(PP_USE_RAII) || defined(PP_USE_CYG) || defined(PP_USE_XRAY) || defined(PP_USE_PLUGIN)
  #include "pathprof/shm.h"
  #include <cstdlib>
  #define PP_INIT() do { const char* e = ::getenv("PATHPROF_SHM"); if (e) ::pathprof::export_shm(e); } while (0)
#else
  #define PP_INIT()
#endif

// ---- wire format -----------------------------------------------------------
enum : uint8_t { ADD='A', DEL='D', EXE='E', CXL='X', REP='U' };
#pragma pack(push, 1)
struct PktHdr { uint32_t seq; uint16_t len; };
struct MsgAdd { uint8_t type; uint64_t ref; uint8_t side; uint32_t shares; uint32_t stock; uint32_t price; };
struct MsgRef { uint8_t type; uint64_t ref; };
struct MsgRefSh { uint8_t type; uint64_t ref; uint32_t shares; };
struct MsgRep { uint8_t type; uint64_t oldref; uint64_t newref; uint32_t shares; uint32_t price; };
#pragma pack(pop)

// ---- order book ------------------------------------------------------------
struct Order { uint64_t ref; uint8_t side; uint32_t shares; uint32_t stock; uint32_t price; };
inline constexpr uint32_t BOOK_SLOTS = 1u << 20;
inline Order g_book[BOOK_SLOTS];
inline uint64_t g_sink = 0;
inline uint32_t book_slot(uint64_t ref) PP_NOINSTR;
inline uint32_t book_slot(uint64_t ref) { uint64_t h = ref * 0x9E3779B97F4A7C15ull; return (uint32_t)(h >> 44) & (BOOK_SLOTS - 1); }

// ---- pipeline --------------------------------------------------------------
PP_FN void consumer(uint64_t ref, uint32_t shares, uint32_t price) {
    PP_BODY("consumer");
    uint64_t acc = g_sink;
    for (int i = 0; i < 8; i++) acc += (ref ^ (uint64_t)price) * (shares + i);
    g_sink = acc ^ (acc >> 13);
}
PP_FN void book_add(uint64_t ref, uint8_t side, uint32_t shares, uint32_t stock, uint32_t price) {
    PP_BODY("book_add");
    uint32_t s = book_slot(ref);
    g_book[s] = Order{ref, side, shares, stock, price};
    g_sink += g_book[s].price;
}
PP_FN void book_delete(uint64_t ref) {
    PP_BODY("book_delete");
    uint32_t s = book_slot(ref);
    g_book[s].ref = 0; g_sink += s;
}
PP_FN void book_cancel(uint64_t ref, uint32_t shares) {
    PP_BODY("book_cancel");
    uint32_t s = book_slot(ref);
    if (g_book[s].shares > shares) g_book[s].shares -= shares; else g_book[s].ref = 0;
    g_sink += g_book[s].shares;
}
PP_FN void book_execute(uint64_t ref, uint32_t shares) {
    PP_BODY("book_execute");
    uint32_t s = book_slot(ref);
    Order& o = g_book[s]; uint32_t px = o.price;
    if (o.shares > shares) o.shares -= shares; else o.ref = 0;
    consumer(ref, shares, px);
}
PP_FN void book_replace(uint64_t oldref, uint64_t newref, uint32_t shares, uint32_t price) {
    PP_BODY("book_replace");
    uint32_t s = book_slot(oldref);
    uint8_t side = g_book[s].side; uint32_t stock = g_book[s].stock;
    book_delete(oldref);
    book_add(newref, side, shares, stock, price);
}
PP_FN void itch_parse(const uint8_t* p, uint16_t len) {
    PP_BODY("itch_parse");
    (void)len;
    switch (p[0]) {
        case ADD: { auto m=(const MsgAdd*)p;  book_add(m->ref,m->side,m->shares,m->stock,m->price); break; }
        case DEL: { auto m=(const MsgRef*)p;  book_delete(m->ref); break; }
        case EXE: { auto m=(const MsgRefSh*)p;book_execute(m->ref,m->shares); break; }
        case CXL: { auto m=(const MsgRefSh*)p;book_cancel(m->ref,m->shares); break; }
        case REP: { auto m=(const MsgRep*)p;  book_replace(m->oldref,m->newref,m->shares,m->price); break; }
        default: break;
    }
}
inline uint32_t g_expect_seq = 0;
PP_FN void packet_parse(const uint8_t* pkt) {
    PP_BODY("packet_parse");
    auto h = (const PktHdr*)pkt;
    g_sink += (h->seq == g_expect_seq);
    g_expect_seq = h->seq + 1;
    itch_parse(pkt + sizeof(PktHdr), h->len);
}

// ---- fake socket -----------------------------------------------------------
struct Stream { std::vector<uint8_t> buf; std::vector<uint32_t> off; };
inline uint64_t g_xrng = 0x243F6A8885A308D3ull;
inline uint64_t xnext() PP_NOINSTR;
inline uint64_t xnext() { g_xrng ^= g_xrng<<13; g_xrng ^= g_xrng>>7; g_xrng ^= g_xrng<<17; return g_xrng; }

inline Stream gen_stream(uint32_t n) PP_NOINSTR;
inline Stream gen_stream(uint32_t n) {
    Stream s; s.buf.reserve((size_t)n * 40); s.off.reserve(n);
    uint32_t seq = 0;
    auto put = [&](const void* m, uint16_t mlen) {
        s.off.push_back((uint32_t)s.buf.size());
        PktHdr h{seq++, mlen};
        auto hp=(const uint8_t*)&h; s.buf.insert(s.buf.end(), hp, hp+sizeof(h));
        auto mp=(const uint8_t*)m;  s.buf.insert(s.buf.end(), mp, mp+mlen);
    };
    uint64_t live = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = xnext() % 100;
        uint64_t ref = 1 + (xnext() % (live ? live : 1));
        if (r < 60)      { MsgAdd  m{ADD, ++live, (uint8_t)(xnext()&1), (uint32_t)(xnext()%1000+1), (uint32_t)(xnext()%500), (uint32_t)(xnext()%100000)}; put(&m,sizeof(m)); }
        else if (r < 85) { MsgRef  m{DEL, ref}; put(&m,sizeof(m)); }
        else if (r < 93) { MsgRefSh m{CXL, ref, (uint32_t)(xnext()%100+1)}; put(&m,sizeof(m)); }
        else if (r < 98) { MsgRefSh m{EXE, ref, (uint32_t)(xnext()%100+1)}; put(&m,sizeof(m)); }
        else             { MsgRep  m{REP, ref, ++live, (uint32_t)(xnext()%1000+1), (uint32_t)(xnext()%100000)}; put(&m,sizeof(m)); }
    }
    return s;
}

// ---- driver ----------------------------------------------------------------
inline int run(int argc, char** argv, const char* mode) PP_NOINSTR;
inline int run(int argc, char** argv, const char* mode) {
    PP_INIT();                  // route to shared memory if PATHPROF_SHM is set
    uint32_t n = (argc > 1) ? (uint32_t)strtoul(argv[1], nullptr, 10) : 2'000'000;
    Stream s = gen_stream(n);
    uint32_t pkts = (uint32_t)s.off.size();

    int loops = 1;              // PATHPROF_LOOPS replays the stream (for live demos)
    if (const char* L = getenv("PATHPROF_LOOPS")) loops = atoi(L);

    // Calibrate TSC->ns before the measured region (this chrono use is wiped by
    // PP_RESET). We time the loop with rdtsc only, so the measured region calls
    // no instrumented STL - which keeps the cyg front-end's slab clean.
    auto wc0 = std::chrono::steady_clock::now(); uint64_t rc0 = __rdtsc();
    while (std::chrono::steady_clock::now() - wc0 < std::chrono::milliseconds(5)) {}
    uint64_t rc1 = __rdtsc(); auto wc1 = std::chrono::steady_clock::now();
    double tsc_hz = (rc1 - rc0) / std::chrono::duration<double>(wc1 - wc0).count();

    // Hoist raw pointers so the hot loop touches no std::vector accessors.
    const uint8_t*  base = s.buf.data();
    const uint32_t* off  = s.off.data();

    PP_RESET();                 // drop init/warmup noise (esp. cyg auto-instrumentation)
    uint64_t c0 = __rdtsc();
    for (int rep = 0; rep < loops; rep++)
        for (uint32_t i = 0; i < pkts; i++) {
            PP_ROOT();
            packet_parse(base + off[i]);
        }
    uint64_t c1 = __rdtsc();

    uint64_t total = (uint64_t)pkts * loops;
    double cyc = (double)(c1 - c0) / total;
    fprintf(stderr, "mode=%-13s pkts=%llu  %.3f s  %.2f ns/pkt  %.1f cyc/pkt  sink=%llu\n",
            mode, (unsigned long long)total, (c1 - c0) / tsc_hz, cyc / tsc_hz * 1e9, cyc,
            (unsigned long long)g_sink);
    PP_SUMMARY(12);
    return 0;
}
