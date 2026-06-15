#pragma once

#include <cstdint>

namespace qstorage::db {
/*
定义 allocator_root_id 类型为 64 位无符号整数（uint64），
用于标识分配器的「根 ID」（可理解为分配器实例 / 超级块的唯一标识）。

分配器需要管理多个超级块（super block），每个超级块对应一个 
allocator_root_id，用于定位、分配、删除超级块地址。

*/

using AllocatorRootID = uint64_t;

// NVALID_ALLOCATOR_ROOT_ID 为 0，作为「无效的分配器根 ID」的标识。
const AllocatorRootID INVALID_ALLOCATOR_ROOT_ID = 0;

// AL_NO_REFS（1）是一个「过渡状态」，用于防止并发操作下的野指针（例如：计数从 2 减到 1 时，先标记为无引用，再异步释放）。
enum class RcStatus : uint8_t {
    AL_FREE = 0,  // 内存页处于「空闲」状态，无任何引用，可被重新分配。
    AL_NO_REFS =
        1,  // 内存页「无有效引用」（但未完全释放），是引用计数递减后的中间状态。
    AL_ONE_REF =
        2  // 内存页有「1 个有效引用」（核心状态，标识页正在被使用，不可释放）。
};

enum class PageType : uint8_t {
    PAGE_TYPE_INVALID = 0,              // 无效页面类型（默认/错误标记）
    PAGE_TYPE_FIRST = 1,                // 第一个有效页面类型的锚点
    PAGE_TYPE_TRUNK = PAGE_TYPE_FIRST,  // 主干页（B树的顶层/索引页）
    PAGE_TYPE_BRANCH = 2,               // 分支页（B树的中间层）
    PAGE_TYPE_MEMTABLE = 3,             // 内存表页（写入时的临时内存结构）
    PAGE_TYPE_FILTER = 4,               // 过滤页（如布隆过滤器，优化查询）
    PAGE_TYPE_LOG,                      // 日志页（事务/写前日志 WAL）
    PAGE_TYPE_BLOB,                     // 大对象页（存储原始用户大数据）
    PAGE_TYPE_SUPERBLOCK,               // 超级块页（存储元数据、全局配置）
    PAGE_TYPE_MISC,                     // 杂项页（测试用，缓存访问测试）
    NUM_PAGE_TYPES,                     // 页面类型总数（用于边界检查）
};

const char* const PageTypeStr[] = {"invalid",  "trunk",      "branch",
                                   "memtable", "filter",     "log",
                                   "blob",     "superblock", "misc"};

/*
PAGE_TYPE_TRUNK	
+ struct trunk_hdr{}	
+ B 树的主干（顶层）页面，存储<key, trunk_pivot_data>数组，指向分支页
PAGE_TYPE_BRANCH 
+ struct btree_hdr{}	
+ B 树的分支（中间层）页面，承接主干页，指向叶子 / 数据页
PAGE_TYPE_MEMTABLE	
+ struct btree_hdr{}	
+ 内存表页面，写入时先落内存（避免直接写磁盘），后续异步刷盘
PAGE_TYPE_FILTER	
+ 自定义优化结构	
+ 过滤页（如布隆过滤器），快速判断 key 是否存在，减少磁盘 IO
PAGE_TYPE_LOG	
+ struct shard_log_hdr{}	
+ 日志页（WAL），记录数据修改，保证崩溃后数据恢复
PAGE_TYPE_BLOB	
+ 原始用户数据（blob.c）	
+ 存储大对象（BLOB），直接存放用户的原始大尺寸数据
PAGE_TYPE_SUPERBLOCK	
+ struct trunk_super_block{}	
+ 超级块，存储整个存储引擎的元数据（如版本、根节点位置、配置）
PAGE_TYPE_MISC	
+ 无特定结构	
+ 测试专用，用于缓存访问、页面调度等逻辑的测试
*/
}  // namespace qstorage::db