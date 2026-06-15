#include <cstddef>
#include <cstdint>
#include "list.h"

namespace qstorage::tools {

class Block {};

class BufferPool {
   public:
   private:
    ListHead<Block> free_;  // 空闲双向链表
    ListHead<Block> lru_;  //活跃页面双向主链表,全局冷热排序，内存不足时尾部淘汰
    //指向冷区第一个 block,逻辑拆分冷热区，解决批量扫描缓存污染
    Block* lru_old_;
    size_t lru_old_len_;  // 冷区页面总数,控制冷热区比例，动态维护分割指针
};
}  // namespace qstorage::tools