#pragma once

#include <folly/coro/Mutex.h>
#include <folly/coro/SharedMutex.h>
#include <atomic>
#include "list.h"

namespace qstorage::tools {
constexpr static size_t LOG_PAGE_SZIE = 8 * 1024;

class LogBufferBlock final {
    uint8_t* frame; /*一块大小PAGE_SIZE对齐的内存*/
    uint64_t pos;
    uint8_t state;
    folly::coro::SharedMutex rw_mutex;      //读写都可以
    std::atomic<uint64_t> only_read_count;  // 只读
    ListNode<LogBufferBlock> free;
    ListNode<LogBufferBlock> lru;
    size_t clock_time;

    /*
    BUF_BLOCK_NOT_USED：空闲块，在 free 链表
    BUF_BLOCK_FILE_PAGE：磁盘页已加载进内存，读写都可以
    BUF_BLOCK_FILE_PAGE_ONLY_READ：磁盘页已加载进内存，只读
    */

   public:
    void Init(uint8_t* frame, uint8_t state, uint64_t pos) {
        this->pos = pos;
        this->state = state;
        this->frame = frame;
        this->clock_time = 0;
    }
};

template <typename T, size_t N>  // N 表示page_size的大小
class Buffer final {
   private:
    folly::coro::Mutex mutex_;
    size_t size_;  //总页数

    ListBase<T> free;
    ListBase<T> lru;

   public:
    getFreeBlock();
};

}  // namespace qstorage::tools