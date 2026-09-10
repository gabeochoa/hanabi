#include <cstdlib>
#include <new>

#include "prof.h"

void* operator new(std::size_t n) {
    if (hanabi::prof::enabled()) {
        ++hanabi::prof::alloc_count();
        hanabi::prof::alloc_bytes() += n;
        if (hanabi::prof::sites_enabled()) {
            const void* pc[hanabi::prof::kSiteDepth];
            hanabi::prof::capture_site(pc);
            hanabi::prof::record_site(pc, n);
        }
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
