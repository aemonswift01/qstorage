#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include "db/constant.h"

namespace qstorage::db {

class SPSCRingBuffer final {
   public:
    SPSCRingBuffer() {
        static_assert(
            kBufferSize >= kPageSize && ((kBufferSize & (kPageSize - 1)) == 0),
            "Buffer size must be a multiple of page size");
        int fd = shm_open("/qstorage_spsc_double_mapped_buffer",
                          O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd == -1) {
            fd = shm_open("/qstorage_spsc_double_mapped_buffer", O_RDWR, 0600);
            if (fd == -1) {
                throw std::runtime_error(
                    "Failed to create/open shared memory object.");
            }
        }
        // 确保使用后立即断开名字连接（防止残留），fd 仍然有效

        shm_unlink("/qstorage_spsc_double_mapped_buffer");

        if (ftruncate(fd, kBufferSize) == -1) {
            close(fd);
            throw std::runtime_error("Failed to set size of shared memory.");
        }
        size_t cap = kBufferSize * 2 + kBlockSize - kPageSize;
        void* raw_space =
            mmap(NULL, cap, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw_space == MAP_FAILED) {
            throw std::runtime_error("Initial mmap failed");
        }
        buffer_ = reinterpret_cast<uint8_t*>(raw_space);
        if (kBlockSize - kPageSize > 0) {
            munmap(buffer_, kBlockSize - kPageSize);
        }
        buffer_ += kBlockSize - kPageSize;
        if (mmap(buffer_, kBufferSize, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED) {
            throw std::runtime_error("First hardware mapping failed");
        }
        if (mmap(buffer_ + kBufferSize, kBufferSize, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED) {
            throw std::runtime_error("Second hardware mapping failed");
        }
        close(fd);
    }

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    ~SPSCRingBuffer() {
        if (buffer_ != nullptr) {
            munmap(buffer_, kBufferSize * 2);
        }
    }

    uint8_t* RequestWriteSpace(size_t size) {
        size_t cur_tail = tail_.load(std::memory_order_relaxed);
        size_t used_space = head_ - cur_tail;
        size_t free_space = kBufferSize - used_space;
        if (free_space < size) {
            return nullptr;
        }
        // 哪怕取模后的 index 到了末尾，加上 size 越界了，因为双映射的存在，虚拟内存也是连续的！
        size_t head_index = head_ & kBufferSizeMask;
        auto res = buffer_ + head_index;
        head_ += size;
        return res;
    }

    void CommitWrite(size_t size) {
        tail_.fetch_add(size, std::memory_order_relaxed);
    }

   private:
    alignas(kCacheSize) std::atomic<size_t> tail_{0};
    alignas(kCacheSize) size_t head_{0};
    uint8_t* buffer_{nullptr};
};
}  // namespace qstorage::db