#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "allocator.h"
#include "util.h"

namespace qstorage::db {
// 持的最大超级块（superblock）数量（也叫 root ID）。
const uint64_t RC_ALLOCATOR_MAX_ROOT_IDS = 30;

// 每个实例对应一个超级块，每个超级块关联一个映射到该实例的表。
// 该宏限制了可访问的超级块（特殊页）最大数量，且所有这些超级块必须位于第一个存储扩展区（extent） 上。

struct ONDISK RcAllocatorMetaPage {
    AllocatorRootID splinters_[RC_ALLOCATOR_MAX_ROOT_IDS] = {0};
    uint64_t check_sum_ = 0;
};

struct RcAllocatorStats {
    // curr_allocated：当前已分配的扩展区（extent）数量（扩展区是存储资源的分配单位）。
    uint64_t curr_allocated_;
    // max_allocated：历史最大已分配扩展区数量（“高水位标记”，反映分配器的峰值负载）。
    uint64_t max_allocated_;
    // extent_allocs[NUM_PAGE_TYPES]：按页类型（NUM_PAGE_TYPES 是页类型总数，如数据页、索引页、超级块页等）统计的扩展区分配次数。
    uint32_t extent_allocs_[size_t(PageType::NUM_PAGE_TYPES)];
    // extent_deallocs[NUM_PAGE_TYPES]：按页类型统计的扩展区释放次数。
    uint32_t extent_deallocs_[size_t(PageType::NUM_PAGE_TYPES)];
};

class RcAllocator {
    RcAllocatorMetaPage meta_page_;
    // 指向引用计数管理模块的指针，核心是通过引用计数跟踪扩展区的使用，实现自动释放
    std::atomic<uint64_t> ref_count_{0};

    std::mutex lock_;

    RcAllocatorStats stats_;
};
}  // namespace qstorage::db
