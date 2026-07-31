#pragma once
#include <cstdint>
#include "infra/atomic.h"

namespace qstorage::infra {

class SKMVSkipList {
   private:
    struct SKMVNode;
};

/*
start_ts_==current || commit_ts<current
if start_ts have,then commit_ts=maxuint64
if commit_ts have, then start_ts=maxuint64
*/

enum class ValueType : uint8_t {
    kValueRowID = 0,
    kValueFileNum = 1,
    kDelete = 2,
    kDeleteAll = 3,

};

struct CommonValue {
    uint64_t start_ts_;
    uint64_t commit_ts_;
    uint64_t row_id_;
};

struct PKValue {
    uint64_t start_ts_;
    uint64_t commit_ts_;
    uint32_t
};

struct SKMVSkipList::SKMVNode {

    // next_[0] is the lowest level link (level 0).  Higher levels are
    // stored _earlier_, so level 1 is at next_[-1].
    AcqRelAtomic<SKMVNode*> next_[1];  // height
    void* extra_value_;
    uint8_t key_value_[1];
};
};  // namespace qstorage::infra

// 多个mvcc进行聚合，从而降低磁盘的写入率的问题
/*
事务1引用key的1号版本，长时间持有。后面key经过几个版本发生更改。即经历key2,key3,key4,key5版本。
则落盘时只需保留key1和key5版本即可。中间版本可以进行压缩。

*/