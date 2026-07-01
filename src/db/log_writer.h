#pragma once
#include <errno.h>  // errno 定义
#include <fcntl.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Task.h>
#include <folly/io/async/IoUring.h>
#include <folly/io/async/IoUringBackend.h>
#include <liburing.h>
#include <string.h>  // strerror
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include "constant.h"
#include "db/sp_ring_buffer.h"
#include "infra/io_uring.h"
#include "infra/port_posix.h"
#include "infra/serialize.h"
#include "infra/task.h"
#include "ring_buffer.h"
#include "tools/error.h"

namespace qstorage::db {

class LogWriter;

struct WriteTask {

    LogWriter& writer_;
    size_t index_;
    bool is_leader_;
    uint8_t* addr_ = nullptr;

    WriteTask(LogWriter& writer, bool is_leader, size_t index)
        : is_leader_(is_leader), writer_(writer), index_(index) {}

    bool Leader() noexcept { return is_leader_; }

    infra::Task<void> LeaderComplete(uint32_t crc, uint16_t len,
                                     infra::CoIoUring& io_uring) {
        co_await writer_.notify_leader_;
        writer_.notify_leader_.reset();
        infra::SerializeLE(addr_, crc);
        infra::SerializeLE(addr_ + 2, len);

        auto& writer = writer_.writers_[index_];
        while (writer.completed_copy_.load() != writer.count_ - 1) {
            infra::AsmVolatilePause();
        }
        uint8_t* start = reinterpret_cast<uint8_t*>(
            reinterpret_cast<uintptr_t>(addr_) & ~(kBlockSize - 1));
        auto len = writer.len + 6;
        memset(addr_ + writer.len, 0, 6);
        size_t size = (len + kBlockSize - 1) & ~(kBlockSize - 1);
        co_await commit(start, size, writer_.file_pos_, io_uring);
        writer.completion_.post();
        writer_.notify_leader_.post();
        writer.Reset();
        writer_.buffer_.CommitWrite(writer.len);
        writer_.memory_notify_.post();
        co_return;
    }

    infra::Task<void> FollowerComplete() {
        writer_.writers_[index_].completed_copy_.fetch_add(1);
        co_await writer_.writers_[index_].completion_;
        co_return;
    }

   private:
    infra::Task<void> commit(uint8_t* addr, uint16_t len, size_t offset,
                             infra::CoIoUring& io_uring) {
        infra::Baton baton;
        infra::IoRequest req(infra::OpCode::WRITE, writer_.fd_, addr, len,
                             offset, baton);
        io_uring.submit(&req);
        co_return co_await baton;
    }
};

class LogWriter {
   public:
    LogWriter(std::string log_file_path)
        : file_path_(std::move(log_file_path)) {}

    ~LogWriter() {
        if (fd_ > 0) {
            close(fd_);
        }
    }

    tools::Error init() {
        fd_ = open(file_path_.c_str(), O_CREAT | O_RDWR | O_DIRECT | O_DSYNC,
                   0644);
        if (fd_ < 0) {
            int err_code = errno;
            return tools::Error(std::string(strerror(err_code)),
                                tools::ErrorCode(err_code));
        }
        struct stat st{};
        if (fstat(fd_, &st) < 0) {
            int err_code = errno;
            return tools::Error(std::string(strerror(err_code)),
                                tools::ErrorCode(err_code));
        }
        // 已有足够大小，直接跳过fallocate
        if (st.st_size < kLogFileSize) {
            // 为什么需要0初始化，主要解决Unwritten extent的问题
            // Unwritten extent更改需要上元数据锁，而此锁又是排他锁。
            // 现代文件系统（如 Ext4 和 XFS）放弃了老旧的“按块记录（Block Mapping）”方式（那种方式会为每个 4KB 的块写一条记录，太浪费空间），转而采用 Extent（区间） 树。
            // 一个 Extent 实际上是一条三元组记录，类似于：
            // [ 起始逻辑块号(Logical Block), 连续块的数量(Length), 起始物理块号(Physical Block) ]

            // 为了记录 Unwritten 状态，文件系统利用了最高位（Bit）或者专门的标志位：
            // 普通已写入区间(Written)：[0, 1024,500000] —— 表示从逻辑第 0 块开始，连续 1024 个块（共 4MB），对应物理磁盘的 500000 号块。
            // 未写入区间(Unwritten)：[0, 1024, 500000] + Unwritten标志位 —— 同样是这 4MB 的连续物理空间，但被打上了一个未写入的标签。
            // 既然 Unwritten 是以 Extent（可能很大）为单位记录的，那么当你用 O_DIRECT 写入其中一个很小的片段（比如只写了 4KB）时，会发生什么？
            // 会引发严重的元数据锁和性能瓶颈。因为文件系统必须执行 Extent 的分裂（Extent Splitting）：
            // 假设你原本有一个 1MB（256 个 Block）的 Unwritten Extent。现在你用 O_DIRECT 往最中间的第 128 个 Block 写入了 4KB 数据：
            // 磁盘数据写入：4KB 数据直接通过 DMA 写入 SSD。
            // 账本一分为三：原来的一条 Extent记录在元数据层面会被无情地切割成三条新记录：
            //    前段：[Block 0 ~127]->保持 Unwritten
            //    中段：[Block 128]->变成 Written（你刚刚写入的 4KB）
            //    后段：[Block 129 ~255]->保持 Unwritten
            // buffer io在sync下也需要进行元数据的更改
            int ret = fallocate(fd_, FALLOC_FL_ZERO_RANGE, 0, kLogFileSize);
            if (ret != 0) {
                int err_code = errno;
                return tools::Error(std::string(strerror(err_code)),
                                    tools::ErrorCode(err_code));
            }
        }
        // 第一次leader不能阻塞
        notify_leader_.post();
    }

    infra::Task<WriteTask> asyncMalloc(size_t size) {
        bool is_leader = false;
        size_t aw_index = active_writer_.load(std::memory_order_relaxed) & 1;
        co_await lock_.co_lock();
    l1:
        WriteTask task(*this, writers_[aw_index].count_ == 0, aw_index);
        writers_[aw_index].count_++;
        uint8_t* res = buffer_.RequestWriteSpace(size);
        if (res == nullptr) [[unlikely]] {
            co_await memory_notify_;
            memory_notify_.reset();
            aw_index = active_writer_.load(std::memory_order_relaxed) & 1;
            goto l1;
        }
        task.addr_ = res;
        writers_[aw_index].len += size;
        lock_.unlock();
        co_return task;
    }

    friend class WriteTask;

   private:
    alignas(64) struct Writers {
        infra::Baton completion_;
        size_t count_{0};
        size_t len{0};
        std::atomic<size_t> completed_copy_{0};

        void Reset() {
            completion_.reset();
            count_ = 0;
            len = 0;
            completed_copy_.store(0);
        }
    };

    Writers writers_[2];

    std::atomic<size_t> active_writer_{0};

    size_t file_pos_ = 0;

    std::string file_path_;
    int fd_ = 0;
    SPSCRingBuffer buffer_;

    infra::Mutex lock_;

    infra::Baton memory_notify_;
    infra::Baton notify_leader_;
};

// 使用单向链表来做无锁操作
// 为了让write和memcpy并发进行。write时需要将末尾页的6个字节设置为0，这6个字节获取的
//writer作为 leader，进磁盘的write。此writer需要可以不用拷贝前6个字节，后续的字节先进行拷贝。
// 等前面的writer写磁盘写完毕后，再拷贝此6字节。然后作为leader写磁盘。

// 针对分log的即较大的数据放到另外log中。此log方式直接追加，不用管留下6个字节的0.
// 后面有此log来进行真正的做处理。
// 故障恢复的时候只需查此log的最后数据，然后截断前面log的数据
//log为了删除，需要在里面加入主键id。后续删除执行变量主键id是否存在即可。

}  // namespace qstorage::db