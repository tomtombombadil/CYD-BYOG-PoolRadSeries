// The Play Test's heap by who asked for it (Claude's side, Linux): link it
// into the playsim with
//   -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc
// (operator new / delete are replaced here too). Each live block is kept
// with its call site (the caller of malloc / new, a few frames up so a
// helper doesn't hide who called it). heapprof_mark("fight start") prints
// the blocks in use by site, biggest first; at exit the breakdown at the
// highest point the heap reached is printed. Turn the addresses into
// file:line with addr2line -f -C -e playsim (the sites are printed with
// the binary's load offset taken off).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>
#include <map>
#include <new>
#include <unordered_map>
#include <vector>

extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);

namespace {

struct Block {
    size_t size;
    void*  site[3];
};

bool inside = false;                    // (the tables' own allocations aren't tracked)
std::unordered_map<void*, Block>* live = nullptr;
size_t in_use = 0, peak = 0;
std::map<std::vector<void*>, std::pair<size_t, int>> peak_sites;
uintptr_t base = 0;

uintptr_t load_base()
{
    if (!base) {
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&load_base), &info)) base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    }
    return base;
}

void note(void* p, size_t n)
{
    if (!p || inside) return;
    inside = true;
    if (!live) live = new std::unordered_map<void*, Block>;
    void* bt[8] = {};
    const int k = backtrace(bt, 8);
    Block b{n, {k > 2 ? bt[2] : nullptr, k > 3 ? bt[3] : nullptr, k > 4 ? bt[4] : nullptr}};
    (*live)[p] = b;
    in_use += n;
    if (in_use > peak + 1024 || (in_use > peak && peak_sites.empty())) {
        peak = in_use;
        peak_sites.clear();
        for (auto& e : *live) {
            std::vector<void*> key(e.second.site, e.second.site + 3);
            auto& s = peak_sites[key];
            s.first += e.second.size;
            s.second++;
        }
    } else if (in_use > peak) {
        peak = in_use;
    }
    inside = false;
}

void drop(void* p)
{
    if (!p || inside || !live) return;
    inside = true;
    auto it = live->find(p);
    if (it != live->end()) {
        in_use -= it->second.size;
        live->erase(it);
    }
    inside = false;
}

void print(const char* label, const std::map<std::vector<void*>, std::pair<size_t, int>>& sites, size_t total)
{
    std::vector<std::pair<size_t, const std::vector<void*>*>> v;
    std::vector<int> counts;
    for (auto& e : sites) v.push_back({e.second.first, &e.first});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    fprintf(stderr, "[heap] %s: %zu bytes in use\n", label, total);
    const uintptr_t b = load_base();
    int shown = 0;
    for (auto& e : v) {
        if (e.first < 256 || ++shown > 40) break;
        fprintf(stderr, "[heap]   %7zu  %d blocks  sites", e.first, sites.at(*e.second).second);
        for (void* a : *e.second) fprintf(stderr, " 0x%lx", (unsigned long)(reinterpret_cast<uintptr_t>(a) - b - 1));
        fprintf(stderr, "\n");
    }
}

struct AtExit {
    ~AtExit() { if (getenv("HEAPPROF")) print("the highest point", peak_sites, peak); }
} at_exit;

} // namespace

void heapprof_mark(const char* label)
{
    if (!getenv("HEAPPROF") || !live) return;
    inside = true;
    std::map<std::vector<void*>, std::pair<size_t, int>> sites;
    for (auto& e : *live) {
        std::vector<void*> key(e.second.site, e.second.site + 3);
        auto& s = sites[key];
        s.first += e.second.size;
        s.second++;
    }
    print(label, sites, in_use);
    inside = false;
}

extern "C" void* __wrap_malloc(size_t n)
{
    void* p = __real_malloc(n);
    note(p, n);
    return p;
}
extern "C" void __wrap_free(void* p)
{
    drop(p);
    __real_free(p);
}
extern "C" void* __wrap_calloc(size_t a, size_t b)
{
    void* p = __real_calloc(a, b);
    note(p, a * b);
    return p;
}
extern "C" void* __wrap_realloc(void* q, size_t n)
{
    drop(q);
    void* p = __real_realloc(q, n);
    note(p, n);
    return p;
}

void* operator new(size_t n) { void* p = __wrap_malloc(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n) { void* p = __wrap_malloc(n); if (!p) throw std::bad_alloc(); return p; }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return __wrap_malloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return __wrap_malloc(n); }
void operator delete(void* p) noexcept { __wrap_free(p); }
void operator delete[](void* p) noexcept { __wrap_free(p); }
void operator delete(void* p, size_t) noexcept { __wrap_free(p); }
void operator delete[](void* p, size_t) noexcept { __wrap_free(p); }
