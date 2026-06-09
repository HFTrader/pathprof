// A toy market-data pipeline, profiled by path. Tag the hot functions with
// PATHPROF, call pathprof::on_root() once per event, print at the end. That is
// the whole API.
//
// Build:  ./build.sh        (or see the command at the top of build.sh)
// Run:    ./sim 2000000
#include "pathprof/annotate.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

//   fake socket -> event loop -> packet_parse -> itch_parse
//                                  -> book_{add,delete,cancel,execute,replace} -> consumer
// Different ITCH message types take different branches, so each traces a
// different path. Only the PATHPROF-tagged functions appear on the path.

enum : uint8_t { ADD='A', DEL='D', EXE='E', CXL='X', REP='U' };
#pragma pack(push, 1)
struct PktHdr { uint32_t seq; uint16_t len; };
struct MsgAdd { uint8_t type; uint64_t ref; uint8_t side; uint32_t shares; uint32_t stock; uint32_t price; };
struct MsgRef { uint8_t type; uint64_t ref; };
struct MsgRefSh { uint8_t type; uint64_t ref; uint32_t shares; };
struct MsgRep { uint8_t type; uint64_t oldref; uint64_t newref; uint32_t shares; uint32_t price; };
#pragma pack(pop)

struct Order { uint64_t ref; uint8_t side; uint32_t shares; uint32_t stock; uint32_t price; };
static constexpr uint32_t BOOK_SLOTS = 1u << 20;
static Order g_book[BOOK_SLOTS];
static uint64_t g_sink = 0;
static uint32_t slot(uint64_t ref) { return (uint32_t)((ref * 0x9E3779B97F4A7C15ull) >> 44) & (BOOK_SLOTS - 1); }

PATHPROF void consumer(uint64_t ref, uint32_t shares, uint32_t price) {
    uint64_t acc = g_sink;
    for (int i = 0; i < 8; i++) acc += (ref ^ (uint64_t)price) * (shares + i);
    g_sink = acc ^ (acc >> 13);
}
PATHPROF void book_add(uint64_t ref, uint8_t side, uint32_t shares, uint32_t stock, uint32_t price) {
    uint32_t s = slot(ref); g_book[s] = Order{ref, side, shares, stock, price}; g_sink += price;
}
PATHPROF void book_delete(uint64_t ref) { uint32_t s = slot(ref); g_book[s].ref = 0; g_sink += s; }
PATHPROF void book_cancel(uint64_t ref, uint32_t shares) {
    uint32_t s = slot(ref);
    if (g_book[s].shares > shares) g_book[s].shares -= shares; else g_book[s].ref = 0;
}
PATHPROF void book_execute(uint64_t ref, uint32_t shares) {
    uint32_t s = slot(ref); Order& o = g_book[s]; uint32_t px = o.price;
    if (o.shares > shares) o.shares -= shares; else o.ref = 0;
    consumer(ref, shares, px);
}
PATHPROF void book_replace(uint64_t oldref, uint64_t newref, uint32_t shares, uint32_t price) {
    uint32_t s = slot(oldref); uint8_t side = g_book[s].side; uint32_t stock = g_book[s].stock;
    book_delete(oldref);
    book_add(newref, side, shares, stock, price);
}
PATHPROF void itch_parse(const uint8_t* p) {
    switch (p[0]) {
        case ADD: { auto m=(const MsgAdd*)p;   book_add(m->ref,m->side,m->shares,m->stock,m->price); break; }
        case DEL: { auto m=(const MsgRef*)p;   book_delete(m->ref); break; }
        case EXE: { auto m=(const MsgRefSh*)p; book_execute(m->ref,m->shares); break; }
        case CXL: { auto m=(const MsgRefSh*)p; book_cancel(m->ref,m->shares); break; }
        case REP: { auto m=(const MsgRep*)p;   book_replace(m->oldref,m->newref,m->shares,m->price); break; }
    }
}
PATHPROF void packet_parse(const uint8_t* pkt) {
    itch_parse(pkt + sizeof(PktHdr));
}

// plain stream generator (not tagged, so never on the path)
static uint64_t xr = 0x243F6A8885A308D3ull;
static uint64_t xn() { xr ^= xr<<13; xr ^= xr>>7; xr ^= xr<<17; return xr; }
struct Stream { std::vector<uint8_t> buf; std::vector<uint32_t> off; };
static Stream gen(uint32_t n) {
    Stream s; s.buf.reserve((size_t)n*40); s.off.reserve(n); uint32_t seq=0; uint64_t live=1;
    auto put=[&](const void* m, uint16_t l){ s.off.push_back(s.buf.size()); PktHdr h{seq++,l};
        auto hp=(const uint8_t*)&h; s.buf.insert(s.buf.end(),hp,hp+sizeof h);
        auto mp=(const uint8_t*)m;  s.buf.insert(s.buf.end(),mp,mp+l); };
    for (uint32_t i=0;i<n;i++){ uint32_t r=xn()%100; uint64_t ref=1+(xn()%(live?live:1));
        if(r<60){MsgAdd m{ADD,++live,(uint8_t)(xn()&1),(uint32_t)(xn()%1000+1),(uint32_t)(xn()%500),(uint32_t)(xn()%100000)};put(&m,sizeof m);}
        else if(r<85){MsgRef m{DEL,ref};put(&m,sizeof m);}
        else if(r<93){MsgRefSh m{CXL,ref,(uint32_t)(xn()%100+1)};put(&m,sizeof m);}
        else if(r<98){MsgRefSh m{EXE,ref,(uint32_t)(xn()%100+1)};put(&m,sizeof m);}
        else{MsgRep m{REP,ref,++live,(uint32_t)(xn()%1000+1),(uint32_t)(xn()%100000)};put(&m,sizeof m);} }
    return s;
}

int main(int argc, char** argv) {
    uint32_t n = (argc > 1) ? (uint32_t)strtoul(argv[1], nullptr, 10) : 2'000'000;
    Stream s = gen(n);
    for (uint32_t i = 0; i < s.off.size(); i++) {
        pathprof::on_root();                 // reset the running hash for this event
        packet_parse(s.buf.data() + s.off[i]);
    }
    pathprof::summary(12);                    // top paths + per-segment histograms
    printf("(sink %llu)\n", (unsigned long long)g_sink);
    return 0;
}
