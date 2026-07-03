#pragma once
#include <cstddef>
#include <cstdint>

namespace qstorage::infra {

/*
存放方式
[ meta+labels header (skip 个槽) | child 指针数组 (n_children 个槽) | 
 zpath 字节 (按 AlignSize 对齐) | value (仅当 b_is_final) ]

使用前缀树的目的是：在insert过程中，可以把节点落盘。而落盘发生分裂时代价太大了。从子节点到根
节点的所有节点都要进行落盘。否则不能达到快速恢复的问题。也就失去了他的意义。
即出现内存分配充足的节点，在更新的时候也要进行落盘。
 */

//trie针对每个字符只可能出边一次，若为q，即qab，qcd都只会采用q出边。
union ParticaNode {
    struct MetaInfo {
        uint8_t cnt_type_ : 4;  // 决定了最开始要读取几个slot
        uint8_t has_value_ : 1;
        uint8_t zap_len_;
        uint8_t children_label_[2];  //children_label_本质就是出边的字符是什么
    };

    struct BigCount {
        uint16_t unused_;
        uint16_t n_children_;  // cnt_type 为 11/12 时的真实子节点数
    };

    MetaInfo meta_;
    uint32_t child_;
    // 存放child对应的node标记，这里不能使用node标记，只能使用pos位置
    uint8_t chars_[4];    // 表示chilren label标记，即出边字符
    uint8_t bytes_[4];    //表示key中本身的字符串
    BigCount big_count_;  // cnt_type 为 11/12 时的真实子节点数
    uint8_t value_[4];
};

//cnt_type_ 4bit，共16中情况。我们来看如何进行压缩：
static constexpr uint32_t kCntTypeSlots[16] = {
    1,  1, 1,     // 表示1个slot
    2,  2, 2, 2,  // 表示2个slot
    3,  3, 3, 3,  // 表示3个slot
    5,            // 表示5个slot
    10,           //
    0,  0,        // 无效值
    1,            //表示1个slot
};
/*
一个slot占用4字节。
cnt_type:0,子节点为0，故1个slot即可
cnt_type:1,子节点数为1，故1个slot即可
cnt_type:2,子节点数为2，故1个slot即可
cnt_type:3,4,5,6,子节点数为3,4,5,6，故2个slot即可
cnt_type:7,8,9,10,子节点数为7,8,9,10，故3个slot即可
cnt_type:11，表示5个slot，子节点数为11~18个
ccnt_type:12,表示10个slot，子节点数为17~256个，第0个slot为meta，第1个slot为
    rank前缀，共256位，使用位图标记哪些childlabe存在，后续为child label
cnt_type:15,子节点数为256个，使用1个slot即可
*/

}  // namespace qstorage::infra
