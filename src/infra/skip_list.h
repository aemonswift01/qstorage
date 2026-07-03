#pragma once
#include <atomic>
#include <cstdint>
#include <span>

namespace qstorage::infra {
struct TablePrefix {
    uint64_t id_;

    TablePrefix(uint64_t tenant_id, uint64_t db_id, uint64_t table_id)
        : id_(tenant_id << 30 | db_id << 16 | table_id) {}

    uint64_t tenantID() { return id_ >> 30; }

    uint64_t dbID() { return (id_ >> 16) & 0x3fff; }

    uint64_t tableID() { return id_ & 0xffff; }
};

struct Value {

    uint32_t offset_;
    uint32_t file_num_;
};

// 两层跳表，第一层跳表位前缀跳表，值为跳表。

// hash skip list。分配bucket的跳表，然后对key计算hash。分配到对应的bucket中

struct KeyValue {
    std::span<const std::byte> key_;
    Value value_;
};

constexpr size_t kNodeN = 4;

template <size_t N = kN>
struct Node {
    // 最高位占位写，后面几位表示空余位置。
    std::atomic<uint64_t> count_;

    KeyValue data_[N];
    // Array of length equal to the node height.  next_[0] is lowest level link.
    // C language next_[0]
    std::atomic<Node*> next_[1];
};

const int a = sizeof(Node<>);

// 这个map结构是，写只会新增key，不会对key进行重新设置值，但有可能进行扩容

}  // namespace qstorage::infra
