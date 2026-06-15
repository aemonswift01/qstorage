#pragma once

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace qstorage::tools {

template <typename Alloc>
concept IsAllocator = requires(Alloc& a, void* ptr, size_t n) {
    { a.allocate(n) } -> std::same_as<void*>;
    { a.deallocate(ptr) } noexcept -> std::same_as<void>;
};

class SimpleAllocator final {
   public:
    SimpleAllocator() = default;

    [[nodiscard]] void* allocate(size_t n) {
        if (n == 0)
            return nullptr;
        void* ptr = std::malloc(n);
        if (!ptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    void deallocate(void* ptr) noexcept { std::free(ptr); }
};
}  // namespace qstorage::tools
