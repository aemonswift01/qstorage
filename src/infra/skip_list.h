#pragma once
#include <atomic>
#include <compare>
#include <concepts>
#include <cstdint>
#include <span>

#include "infra/atomic.h"

namespace qstorage::infra {

struct Key {
    std::span<const std::byte> key_;
};

// 一个集群不可能去支持千万个租户ID。不然元数据造成大量污染。因此针对大量租户采用了分布式集群。
// 一个分布式数据库管一个租户范围。

struct TablePrefix {
    uint64_t id_;

    TablePrefix(uint64_t tenant_id, uint64_t db_id, uint64_t table_id)
        : id_(tenant_id << 30 | db_id << 16 | table_id) {}

    uint32_t tenantID() { return id_ >> 32; }

    //
    uint32_t dbID() { return (id_ >> 16) & 0x3fff; }

    // 一个库最多1024张表
    uint32_t tableID() { return id_ & 0b11111111110000; }

    // 一张表最多16个索引，0号为主键索引。
    uint32_t indexID() { return id_ & 0b1111; }
};

struct TablePrefixComp {
    std::strong_ordering operator()(const TablePrefix& lhs,
                                    const TablePrefix& rhs) {
        return lhs.id_ <=> rhs.id_;
    }
};

// 一个key由三部分组成，前缀，中缀，后缀
// 后缀可能为空。例如主键对应的key ---value
// 后缀不为空。
enum class ValueType : uint8_t {
    kDelete = 1,                  // 匹配前缀删除，即不用一行一行删除。
    kDeleteContainSuffixKey = 2,  // 删除一行数据
    kContainSuffixKey = 3,
    kContainFileOffset = 4,

};

/*
8个字节：事务号
8个字节：

*/

struct ValueFileOffset {
    //
    uint32_t offset_;
    uint32_t file_num_;
};

struct Value {};

// 两层跳表，第一层跳表位前缀跳表，值为跳表。

// hash skip list。分配bucket的跳表，然后对key计算hash。分配到对应的bucket中

struct KeyValue1 {
    Key key_;
    Value value_;
};

constexpr size_t kNodeN = 16;

template <typename KeyValue, size_t N = kNodeN>
struct Node {
    explicit Node(const KeyValue& key_value) : data_[0](key_value) {}

    Node* Next(int n) {
        assert(n >= 0);
        // Use an 'acquire load' so that we observe a fully initialized
        // version of the returned Node.
        return (next_[n].Load());
    }

    void SetNext(int n, Node* x) {
        assert(n >= 0);
        // Use a 'release store' so that anybody who reads through this
        // pointer observes a fully initialized version of the inserted node.
        next_[n].Store(x);
    }

    // No-barrier variants that can be safely used in a few locations.
    Node* NoBarrier_Next(int n) {
        assert(n >= 0);
        return next_[n].LoadRelaxed();
    }

    void NoBarrier_SetNext(int n, Node* x) {
        assert(n >= 0);
        next_[n].StoreRelaxed(x);
    }

    bool SetKeyValue(const KeyValue& key_value) {
        auto res = count_.load(std::memory_order_acquire);
        if (res >= N) {
            return false;
        }

        while (true) {

            if (count_.compare_exchange_weak(res, res | 1uul << 63,
                                             std::memory_order_acq_rel)) {
                break;
            }
                }

        auto index = count_.fetch_add(1, std::memory_order_relaxed);
        if (index >= N) {
            return false;
        }
        data_[index] = key_value;
        commit_.fetch_add(1, std::memory_order_release);
    }

    // 最高位占位写，后面几位表示空余位置。
    std::atomic<uint64_t> count_{1};

    std::atomic<uint64_t> commit_{1};

    KeyValue data_[N];
    // Array of length equal to the node height.  next_[0] is lowest level link.
    // C language next_[0]
    AcqRelAtomic<Node*> next_[1];
};

// 这个map结构是，写只会新增key，不会对key进行重新设置值，但有可能进行扩容
template <typename T, typename KeyType>
concept KeyComp = requires(T& t) {
    { t(KeyType{}, KeyType{}) } -> std::same_as<std::strong_ordering>;
};

template <typename KeyType, KeyComp<KeyType> Comparator>
class SkipList {
   public:
    explicit SkipList(Comparator cmp) : comparator_(cmp) {}

   private:
    Comparator const compare_;
    Node* const head_;
    std::atomic<size_t> max_height_;
};

// 一个节点包含16个点，节点分为全量节点和压缩节点。索引节点必须为全量节点。即key是全量的内容。

}  // namespace qstorage::infra
