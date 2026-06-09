// pathprof/shm.h - place the aggregate slab in POSIX shared memory so an
// external process (pathprof-top) can render the live path profile without
// touching the traced application. The slab is POD and self-describing
// (segment names are snapshotted in it), so the reader needs no symbols.
//
// Traced process:  pathprof::export_shm("/pathprof");   // before any recording
// Reader:          pathprof::map_shm("/pathprof", false);
// Link both with -lrt (older glibc) for shm_open.
#pragma once
#include "core.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace pathprof {

constexpr uint64_t SLAB_MAGIC = 0x5041544850524F46ull;   // "PATHPROF"

PP_RT inline Slab* map_shm(const char* name, bool create) {
    int fd = shm_open(name, create ? (O_RDWR | O_CREAT) : O_RDWR, 0666);
    if (fd < 0) return nullptr;
    if (create && ftruncate(fd, sizeof(Slab)) != 0) { close(fd); return nullptr; }
    void* p = mmap(nullptr, sizeof(Slab), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return (p == MAP_FAILED) ? nullptr : (Slab*)p;
}

// Route all aggregation into the shared segment. Returns false if mmap failed
// (in which case the in-process slab keeps being used).
PP_RT inline bool export_shm(const char* name) {
    Slab* s = map_shm(name, true);
    if (!s) return false;
    s->magic = SLAB_MAGIC;
    g_slab = s;
    return true;
}

} // namespace pathprof
