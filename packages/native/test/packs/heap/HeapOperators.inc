// Every replaceable form of the global operator new and delete, each through the including file's heapAllocate and
// heapRelease (HeapTag.hpp). Included once per module: HostHeap.cpp in the test binary, PackHeap.cpp in the pack.

void* operator new(std::size_t n) {
    if (void* p = heapAllocate(n, alignof(std::max_align_t))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (void* p = heapAllocate(n, alignof(std::max_align_t))) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, std::align_val_t a) {
    if (void* p = heapAllocate(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
    if (void* p = heapAllocate(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return heapAllocate(n, alignof(std::max_align_t)); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return heapAllocate(n, alignof(std::max_align_t)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return heapAllocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return heapAllocate(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { heapRelease(p); }
void operator delete[](void* p) noexcept { heapRelease(p); }
void operator delete(void* p, std::size_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::size_t) noexcept { heapRelease(p); }
void operator delete(void* p, std::align_val_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::align_val_t) noexcept { heapRelease(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { heapRelease(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { heapRelease(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { heapRelease(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { heapRelease(p); }
