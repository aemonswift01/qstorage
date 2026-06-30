#pragma once
#include <cstddef>

namespace qstorage::db {
// 需要根据ssd的页大小来进行确定。因为ssd是按照页单位来操作的。有可能是4k，8k，16k
static constexpr size_t kBlockSize = 8 * 1024;
// kBlockSize 对齐单位
static constexpr size_t kLogFileSize = 1024 * 1024 * 1024;
static constexpr size_t kCacheSize = 64;

static constexpr size_t kBufferSize = 8 * 1024 * 1024;
static constexpr size_t kBufferSizeMask = kBufferSize - 1;

static constexpr size_t kPageSize = 4 * 1024;

}  // namespace qstorage::db