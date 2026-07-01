#pragma once

#include <liburing.h>
#include "infra/bounded_queue.h"
#include "infra/task.h"

namespace qstorage::infra {

static inline unsigned SqSpace(const struct io_uring* ring) {
    // 剩余空间 = 环的总容量 - (当前用户写指针 - 内核已消费指针)
    return ring->sq.ring_entries - (*ring->sq.ktail - *ring->sq.khead);
}

unsigned int CqReadyCount(struct io_uring* ring) {
    // io_uring_cq_ready 返回当前 CQ 队列中可读的事件数量
    return io_uring_cq_ready(ring);
}

enum class OpCode { READ = IORING_OP_READ, WRITE = IORING_OP_WRITE };

struct IoRequest {
    IoRequest(OpCode op_code, int fd, void* addr, size_t size, size_t offset,
              infra::Baton& baton)
        : baton_(baton),
          op_code_(op_code),
          fd_(fd),
          addr_(addr),
          size_(size),
          offset_(offset) {}

    OpCode op_code_;
    ssize_t res_;
    int fd_;
    void* addr_;
    size_t size_;
    size_t offset_;
    infra::Baton& baton_;
};

class CoIoUring {
   public:
    CoIoUring() = default;
    ~CoIoUring() = default;
    CoIoUring(const CoIoUring&) = delete;
    CoIoUring& operator=(const CoIoUring&) = delete;

    Task<void> submit(IoRequest* req) {
        co_return co_await queue_.co_enqueue(req);
    }

    void runLoop() {
        while (true) {
            auto ioc = queue_.size();
            auto cqc = CqReadyCount(ring_);
            if (ioc == 0 && cqc == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (ioc > 0) {
                size_t sq_space = SqSpace(ring_);
                auto len = std::min(ioc, sq_space);
                len = std::min(len, max_submits_);
                for (int i = 0; i < len; i++) {
                    IoRequest* io = queue_.dequeue();
                    io_uring_sqe* sqe = io_uring_get_sqe(ring_);
                    io_uring_prep_rw(int(io->op_code_), sqe, io->fd_, io->addr_,
                                     io->size_, io->offset_);
                    // 统一绑定上下文指针到 user_data
                    io_uring_sqe_set_data(sqe, io);
                }
                int ret = io_uring_submit(ring_);
                assert(ret == len);
                // if (ret >= 0) [[likely]] {
                //     assert(ret == len);
                // } else {
                //     //todo
                // }
            }

            struct io_uring_cqe* cqe = nullptr;
            unsigned head;
            unsigned count = 0;
            io_uring_for_each_cqe(ring_, head, cqe) {
                processComplete(reinterpret_cast<IoRequest*>(cqe->user_data),
                                cqe->res);
                ++count;
            }
            io_uring_cq_advance(ring_, count);
        }
    }

   private:
    void processComplete(IoRequest* req, ssize_t res);
    infra::BoundedQueue<IoRequest*> queue_;
    size_t max_submits_ = 5;
    struct io_uring* ring_{nullptr};
};

}  // namespace qstorage::infra

/*
io_uring的使用方式：
1. 每个io协程io_uring_submit，io_uring_submit本身存在系统调用，影响性能。一个线程针对CQ操作。详见quant项目
2. 一个线程io_uring_submit，然后等待CQ操作。项目3fs
3. 一个线程io_uring_submit，另外一个线程CQ操作
4. 一个线程完成io_uring_submit和CQ操作。

*/