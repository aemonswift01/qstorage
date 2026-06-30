#pragma once
#include <atomic>
#include <cstddef>

namespace qstorage::db {

struct Slice {
    uint8_t* start_;
    uint8_t* end_;
};

class RingBuffer {
   public:
    static constexpr size_t kBufferSize = 8 * 1024 * 1024;
    static constexpr size_t kBufferSizeMask = kBufferSize - 1;

    Slice RequestWriteSpace(size_t size) {
        size_t cur_head = head_.load(std::memory_order_acquire);
        while (true) {
            size_t cur_tail = tail_.load(std::memory_order_relaxed);
            size_t used_space = cur_tail - cur_head;
            size_t free_space = kBufferSizeMask - used_space;
            if (free_space < size) {
                return Slice{nullptr, nullptr};
            }
            if (head_.compare_exchange_weak(cur_head, cur_head + size,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
                break;
            }
        }

        return Slice{&buffer_[0] + (cur_head & kBufferSizeMask),
                     &buffer_[0] + (cur_head + size & kBufferSizeMask)};
    }

    void CommitWrite(size_t size) {
        tail_.fetch_add(size, std::memory_order_relaxed);
    }

   private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    uint8_t buffer_[kBufferSize];
};

}  // namespace qstorage::db
