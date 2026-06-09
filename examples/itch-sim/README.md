# itch-sim

A toy market-data pipeline profiled by call path, using the attribute front-end.

```cpp
PATHPROF void book_add(...) { ... }      // tag the hot functions

for (each event) {
    pathprof::on_root();                 // reset the running hash per event
    packet_parse(...);
}
pathprof::summary(12);                   // top paths + per-segment histograms
```

That is the entire API surface: tag with `PATHPROF`, reset per event, print.
The pass only instruments tagged functions, so nothing else needs configuring.

```bash
./build.sh          # builds the plugin + ./sim (clang)
./sim 2000000
```

Output is the top paths by count with a per-segment cycle histogram for each:

```
#1  count=600848 (58.9%)  path: packet_parse > itch_parse > book_add
      [0] packet_parse  p50=24  p99=384  ...
      [2] book_add      p50=24  p99=1536 ...
```

The other injection front-ends (RAII macro, `-finstrument-functions`, XRay) live
in the library headers; the four-way overhead comparison is under `bench/`.
