#include "buffer.h"

namespace qstorage::tools {

template <typename T, size_t N>
folly::coro::Task<T*> Buffer<T, N>::getFreeBlock() {
    co_await mutex_.co_lock();
    if (free_.count_ > 0) {
        auto* block = free_.start_;
        Remove<&T::free>(free_, *block);
        mutex_.unlock();
        co_return block;
    }
}

}  // namespace qstorage::tools