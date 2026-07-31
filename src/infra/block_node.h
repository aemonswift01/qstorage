#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include "infra/atomic.h"

namespace qstorage::infra {

constexpr int MAX_HEIGHT = 16;
// 单节点容纳 8 个 Key，正好占满几个 Cache Line
constexpr size_t MAX_KEYS_PER_BLOCK = 8;

// 此结构出现问题：
/*
1. 并发问题复杂
2. 容量存在浪费
*/
struct alignas(8) BlockNode {

    // next_[0] is the lowest level link (level 0).  Higher levels are
    // stored _earlier_, so level 1 is at next_[-1].

    AcqRelAtomic<BlockNode*> next_[1];  // height
    std::atomic<uint64_t> count_;
    std::atomic<uint64_t> commit_;
    // from data_ start to end
    uint16_t keys_offset_[MAX_KEYS_PER_BLOCK];
    // from end to data_[1]
    uint16_t values_offset_[MAX_KEYS_PER_BLOCK + 1];
    uint8_t data_[1];
};

/*
上面情况进行进一步优化：
只有主键key的value是边长的。而其他key的value是定长的。因此可以进一步优化：
单key多value的形式
*/

struct SKMVNode {

    // next_[0] is the lowest level link (level 0).  Higher levels are
    // stored _earlier_, so level 1 is at next_[-1].
    AcqRelAtomic<BlockNode*> next_[1];  // height
    void* extra_value_;
    uint8_t key_value_[1];
};

}  // namespace qstorage::infra