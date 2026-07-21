#pragma once

#include <cstddef>
#include <cstdint>

namespace qstorage::infra {
class Allocator {
   public:
    virtual ~Allocator() {}

    virtual char* Allocate(size_t bytes) = 0;
    virtual char* AllocateAligned(size_t bytes, size_t huge_page_size = 0,
                                  Logger* logger = nullptr) = 0;

    virtual size_t BlockSize() const = 0;
};
}  // namespace qstorage::infra