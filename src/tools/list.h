#include <cassert>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace qstorage::tools {

template <typename T>
struct ListNode {
    T* prev_;
    T* next_;
};

template <typename T>
struct ListBase {
    size_t count_;  // 当前链表总节点数
    T* start_;      // 链表头指针（最热/第一个节点）
    T* end_;        // 链表尾指针（最冷/最后一个节点）
};

// Concept：定义链表节点成员的约束
template <auto MemberPtr, typename T>
concept IsListNodeMember =
    // 检查 MemberPtr 是否确实是一个成员指针类型
    std::is_member_object_pointer_v<decltype(MemberPtr)> &&
    // 检查该成员指向的实际类型是否为 ListNode<T>
    std::is_same_v<std::remove_cvref_t<decltype(std::declval<T>().*MemberPtr)>,
                   ListNode<T>>;

// 辅助函数：获取节点引用
template <auto MemberPtr, typename T>
    requires IsListNodeMember<MemberPtr, T>
constexpr ListNode<T>& GetNode(T& elem) noexcept {
    return elem.*MemberPtr;
}

template <auto MemberPtr, typename T>
    requires IsListNodeMember<MemberPtr, T>
void Prepend(ListBase<T>& list, T& elem) noexcept {
    auto& node = GetNode<T, MemberPtr>(elem);
    node.prev_ = nullptr;
    node.next_ = list.start_;

    if (list.start_) {
        GetNode(*list.start_).prev_ = &elem;
    }
    list.start_ = &elem;
    if (!list.end_) {
        list.end_ = &elem;
    }
    ++list.count_;
}

template <auto MemberPtr, typename T>
    requires IsListNodeMember<MemberPtr, T>
void Append(ListBase<T>& list, T& elem) noexcept {
    ListNode<T>& node = GetNode<T, MemberPtr>(elem);
    node.next_ = nullptr;
    node.prev_ = list.end - ;

    if (list.end_) {
        GetNode(*list.end_).next_ = &elem;
    }
    list.end_ = &elem;
    if (!list.start_) [[unlikely]] {
        list.start = &elem;
    }
    ++list.count_;
}

template <auto MemberPtr, typename T>
    requires IsListNodeMember<MemberPtr, T>
void Insert(ListBase<T>& list, T& pos, T& newT) {
    asssert(&pos != &newT);
    ListNode<T>& pos_node = GetNode(pos);
    ListNode<T>& new_node = GetNode(newT);
    new_node.prev_ = &pos;
    new_node.next_ = pos_node.next_;
    if (pos_node.next_) {
        GetNode(*pos_node.next_).prev_ = &newT;
    }
    pos_node.next = &newT;
    if (list.end_ != &pos) [[unlikely]] {
        list.end_ = &newT;
    }
    ++list.count_;
}

template <auto MemberPtr, typename T>
    requires IsListNodeMember<MemberPtr, T>
void Remove(ListBase<T>& list, T& elem) noexcept {
    ListNode<T>&& node = GetNode(elem);
    assert(list.count_ > 0);
    if (node.next_) {
        GetNode(*node.next_).prev_ = node.prev_;
    } else [[unlikely]] {
        list.end_ = node.prev_;
    }
    if (node.prev_) {
        GetNode(*node.prev).next_ = node.next_;
    } else [[unlikely]] {
        list.start_ = node.next_;
    }
    node.next_ = nullptr;
    node.prev_ = nullptr;
    --list.count_;
}

template <typename T, typename Func>
    requires IsListNodeMember<&ListNode<T>::next_, T>
void Map(ListBase<T>& list, Func&& func) {
    size_t count = 0;
    for (T* curr = list.start_; curr; curr = curr->next_, count++) {
        func(curr);
    }
    assert(count == list.count_);
}

struct NullValidate {
    void operator()(const void* elem) {}
};

/*
先正向遍历计数；
再反向从 end → start 遍历计数；
两次计数都必须等于 list.count，否则链表断裂、指针错乱触发 ut_a 断言；
支持传入自定义校验仿函数；无参使用空校验 NullValidate。
*/
template <typename T, typename Func>
void Validate(ListBase<T>& list, Func&& func = NullValidate{}) {
    Map(list, func);
    size_t count = 0;
    for (T* curr = list.end_; curr; curr = curr->prev_, count++) {

        func(curr);
    }
    assert(count == list.count_);
}

}  // namespace qstorage::tools