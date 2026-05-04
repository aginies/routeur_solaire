#ifndef PSRAM_ALLOCATOR_H
#define PSRAM_ALLOCATOR_H

#include <stddef.h>
#include <memory>

#ifndef NATIVE_TEST
#include "esp_heap_caps.h"
#endif

/**
 * Custom C++ allocator that forces allocations into external PSRAM (SPIRAM)
 * when available. On hardware without PSRAM (like WROOM), it falls back
 * to standard internal SRAM.
 */
template <typename T>
class PsramAllocator {
public:
    typedef T value_type;

    PsramAllocator() noexcept {}

    template <typename U>
    PsramAllocator(const PsramAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        if (n == 0) return nullptr;
        
        void* ptr = nullptr;
#if defined(BOARD_HAS_PSRAM) && !defined(NATIVE_TEST)
        // Force allocation into SPIRAM
        ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        
        // Fallback to internal if PSRAM is full or fails
        if (!ptr) {
            ptr = malloc(n * sizeof(T));
        }
#else
        // Standard heap for WROOM or Native Tests
        ptr = malloc(n * sizeof(T));
#endif

        if (!ptr) throw std::bad_alloc();
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, size_t n) noexcept {
#if defined(BOARD_HAS_PSRAM) && !defined(NATIVE_TEST)
        heap_caps_free(p);
#else
        free(p);
#endif
    }

    template <typename U>
    struct rebind {
        typedef PsramAllocator<U> other;
    };

    bool operator==(const PsramAllocator&) const noexcept { return true; }
    bool operator!=(const PsramAllocator&) const noexcept { return false; }
};

#endif // PSRAM_ALLOCATOR_H
