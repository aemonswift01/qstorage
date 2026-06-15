#include <iostream>
#include "list.h"

struct BlockHeader {
    int a_;
    int b_;
    qstorage::tools::ListNode<BlockHeader> free;
};

int main(void) {
    BlockHeader header{.a_ = 1, .b_ = 2};
    BlockHeader b{.a_ = 6, .b_ = 7};
    header.free.prev_ = &b;
    qstorage::tools::GetNode<&BlockHeader::free>(header);
    std::cout << header.free.prev_->a_ << std::endl;
    return 0;
}