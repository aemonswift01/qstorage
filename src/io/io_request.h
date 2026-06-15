#pragma once

#include <cstdint>
#include <string>

#include <vector>

#include <folly/io/async/IoUring.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace qstorage::io {
class IoRequest {
   public:
    enum class IoRequestType : uint8_t {
        read,
        readv,
        write,
        writev,
        fdatasync,
        recv,
        recvmsg,
        send,
        sendmsg,
        accept,
        connect,
        poll_add,
        poll_remove,
        cancel
    };

   private:
    struct read_op {
        IoRequestType op;
        // 是否支持 “不等待直接返回” 模式。对应linux标记RWF_NOWAIT
        // 并非所有文件系统都支持非阻塞 I/O（RWF_NOWAIT）。如果一个设备
        //不支持非阻塞读，当你带上 RWF_NOWAIT 标志位去请求时，内核会直接报错EAGAIN。
        bool nowait_works;
        int fd;
        // 从文件的哪个位置开始读 → 文件偏移量,需要4k对齐
        uint64_t pos;
        // O_DIRECT 绕过 page cache，内核直接和硬件 DMA 控制器传输内存与磁盘数据；NVMe/SSD/ 文件系统块单元固定 4K，
        // 硬件只接受 4K 起始的内存地址，非对齐会直接返回 -EINVAL IO 失败。
        // 因此需要4k对齐
        char* addr;
        // 读多少字节，Direct IO 必须是 4K 的整数倍！
        size_t size;
    };

    // 一次 IO 读取，把数据写到 多块不连续的内存里（多个 buffer）。
    struct readv_op {
        IoRequestType op;
        bool nowait_works;
        int fd;
        // 从文件哪个偏移开始读
        uint64_t pos;
        iovec* iovec;
        size_t iov_len;
    };

    /*
    struct iovec {
    void  *iov_base;  // 内存地址
    size_t iov_len;   // 长度
};
    */
    // recv(fd, buf, size, flags)
    // 专门用于 Socket 网络收数据，不是磁盘 IO～
    // 不需要4k对齐
    struct recv_op {
        IoRequestType op;
        int fd;
        char* addr;
        size_t size;
        int flags;
        /*
        MSG_DONTWAIT     // 非阻塞
        MSG_PEEK         // 偷看数据，不拿走
        MSG_PEEK: 极高阶用法。窥探数据但不消耗它。工业级框架在处理自定义协议头时，
        会先 Peek 一下判断包长度，再决定分配多大的 Buffer。
        MSG_WAITALL      // 等待收满整个缓冲区
        MSG_NOSIGNAL     // 禁止 SIGPIPE 信号
        */
    };

    // 一次接收 + 多块分散内存 + 带文件描述符传递 + 带源地址 + 带控制信息
    struct recvmsg_op {
        IoRequestType op;
        int fd;
        msghdr* msghdr;
        int flags;
    };

    /*
    struct msghdr {
    void         *msg_name;       // 源地址（IP+端口）
    用于 UDP 接收/发送，存储远端 IP 和端口。TCP 连接中通常置为 nullptr。
    socklen_t     msg_namelen;    // 地址长度
    struct iovec *msg_iov;        // 多块内存数组
    size_t        msg_iovlen;     // 数组个数
    void         *msg_control;    // 控制信息
    传递高级控制信息，如文件描述符传递、时间戳、Socket 选项。
    socklen_t     msg_controllen; // 控制信息长度
    int           msg_flags;
    MSG_TRUNC (Data Truncation)--数据截断
    触发场景： 你提供的 iovec 缓冲区总容量，小于接收到的数据包大小。
    不同协议的行为差异（极为重要）：
    在 UDP (Datagram) 中： 如果数据包大于你的缓冲区，内核会填满你的缓冲区，然后直接丢弃数据包剩余的部分。
        这意味着剩下的数据永远丢失了。
    在 TCP (Stream) 中： TCP 是流式协议，它不会“丢包”。如果缓冲区满了，它会把多余的数据留在内核的接收队列中，
        下次调用 recvmsg 时，你会继续读取剩下的数据。但在某些特殊情况下（如设置了 MSG_TRUNC 作为输入参数，或者特定的流控制），行为会有所不同。
    MSG_CTRUNC (Control Data Truncation) 控制信息被截断
    触发场景： 你提供的 msg_control 缓冲区容量太小，无法放下内核准备给你的所有辅助数据（Ancillary Data）。
    常见危险案例：
    你正在使用 Unix Domain Socket 接收文件描述符（SCM_RIGHTS）。
    此时如果触发 MSG_CTRUNC，这意味着传递给你的文件描述符可能被损坏或未成功送达。
        };

为了在你的框架中彻底避免这两个截断问题，你可以采取以下工业级防御措施：
预分配冗余空间 (Over-provisioning)：
对于 msg_control，绝不要按“刚好够用”的字节数去分配。建议直接分配一个页（4KB）的 msg_control 缓冲区。对于绝大多数场景（传递 FD 或时间戳），4KB 绰绰有余，且能彻底消除 MSG_CTRUNC 的风险。
对于 UDP 数据包，确保缓冲区至少能容纳 MTU（通常为 1500 字节，但考虑到 Jumbo Frames 可能达到 9000 字节，对于高性能网络，分配 64KB 的缓冲区是行业标准做法）。
协议头嗅探（Header-First Reading）：
如果不确定包大小，不要直接 recvmsg 全量数据。
先用 MSG_PEEK 查看包头（Header），解析出包的真实长度（Body Length），然后再根据长度分配（或从池中获取）精确大小的缓冲区进行读取。这能极大减少内存碎片和内存浪费，同时规避 MSG_TRUNC。
一句话总结：
在异步 I/O 框架中，MSG_TRUNC 是对性能的警告（缓冲区太小导致效率下降），而 MSG_CTRUNC 是对完整性的警告（控制协议损坏）。作为框架设计者，优先保证控制缓冲区（Control Buffer）永远不会发生截断，是系统稳定性的基石。

recvmsg 比 recv 强在哪里？
recv：只能写一块内存
recvmsg：能写N 块分散内存（scatter）
recv：拿不到发送方地址
recvmsg：UDP 能直接拿到谁发的数据
recv：不能传文件描述符
recvmsg：支持跨进程传递 fd

    */

    using send_op = recv_op;
    using sendmsg_op = recvmsg_op;
    using write_op = read_op;
    using writev_op = readv_op;

    struct fdatasync_op {
        IoRequestType op;
        int fd;
    };

    struct accept_op {
        IoRequestType op;
        int fd;
        // 客户端的地址（IP + 端口）
        sockaddr* sockaddr;
        // 地址长度指针
        // 输入：你告诉内核地址缓冲区多大
        // 输出：内核返回真实地址长度
        // 必须是指针，因为内核要修改它的值。
        socklen_t* socklen_ptr;
        int flags;
        // SOCK_NONBLOCK   // 新连接 fd 默认为非阻塞
        // SOCK_CLOEXEC  // 新连接 fd 带 close-on-exec,在执行 exec() 系列函数（启动新程序）时自动关闭该文件描述符。
        // 父进程创建的 fd，在 fork / exec 后，会全部 遗传 给子进程！
    };

    struct connect_op {
        IoRequestType op;
        int fd;
        sockaddr* sockaddr;
        //  sockaddr 占多少字节。
        socklen_t socklen;
    };

    // libaio: iocb 提交 POLL 命令
    //io_uring : SQE 提交 POLL_ADD 命令
    // 告诉内核：这个 fd 现在还不能读写，你帮我盯着，等能读 / 能写了再通知我！
    struct poll_add_op {
        IoRequestType op;
        int fd;
        int events;
        // 你想监听什么事件？就是你要等 fd 变成什么状态 才唤醒。
        // POLLIN      // 有数据可以读（recv/read）
        // POLLOUT      // 可以发送数据了（send/write）
        // POLLERR  // 发生错误
        // POLLHUP  // 对端断开连接
    };

    // 取消之前提交的 POLL 监听
    struct poll_remove_op {
        IoRequestType op;
        int fd;
        // 它是你要取消的那个 poll 请求的标识！
        // 同一个 fd，可以同时提交多个不同的 POLL 监听
        // fd 5，监听 POLLIN → user_data = ptrA
        // fd 5，监听 POLLOUT → user_data = ptrB
        char* addr;
    };

    // 取消一个正在等待 / 正在处理的异步 IO 请求
    struct cancel_op {
        IoRequestType op;
        int fd;
        // 当初提交 IO 时存在 iocb / SQE 里的 user_data（私有指针）
        char* addr;
    };

    IoRequestType OpType() const noexcept { return cancel_.op; }

    union {
        read_op read_;
        readv_op readv_;
        recv_op recv_;
        recvmsg_op recvmsg_;
        send_op send_;
        sendmsg_op sendmsg_;
        write_op write_;
        writev_op writev_;
        fdatasync_op fdatasync_;
        accept_op accept_;
        connect_op connect_;
        poll_add_op poll_add_;
        poll_remove_op poll_remove_;
        cancel_op cancel_;
    };

    // 一个大 IO 被切开后，生成的「小分片请求」
    // 你要读 1MB
    // 但内核 / 磁盘一次最多只能读 128KB Seastar
    // 就自动把它切成 8 个 128KB 的小请求
    struct IoPart {
        // 切分后的「小 IO 请求」 它是一个完整、正常、可直接提交内核的 io_request
        qstorage::io::IoRequest req;
        // 当前这个小请求要读写多少字节。
        size_t size;
        // 如果是向量 IO（readv /writev），这里存切分后的 iovec 数组
        // 如果是普通 read /write，这里为空。
        std::vector<iovec> iovecs;
    };

    enum class IoDir { write = 0, read = 1 };

    struct IoDirAndLen {
       private:
        size_t len_dir_;  // bit 0 is R/W flag

       public:
        size_t Len() const noexcept { return len_dir_ >> 1; }

        IoDir Direction() const noexcept {
            return static_cast<IoDir>(len_dir_ & 1);
        }

        IoDirAndLen(IoDir dir, size_t len) noexcept
            : len_dir_(len << 1 | size_t(dir)) {}
    };

   public:
    std::vector<IoPart> Split(size_t max_length);
    std::vector<IoPart> SplitIovec(size_t max_length);
    std::vector<IoPart> SplitBuffer(size_t max_length);

   private:
    IoRequest SubReqBuffer(size_t pos, size_t len) const noexcept {
        IoRequest sub_req;
        // read_op and write_op share the same layout, so we don't handle
        // them separately
        auto& op = read_;
        auto& sub_op = sub_req.read_;
        sub_op = {
            .op = op.op,
            .nowait_works = op.nowait_works,
            .fd = op.fd,
            .pos = op.pos + pos,
            .addr = op.addr + pos,
            .size = len,
        };
        return sub_req;
    }

    IoRequest SubReqIovec(size_t pos, std::vector<iovec>& iov) const noexcept {
        IoRequest sub_req;
        // readv_op and writev_op share the same layout, so we don't handle
        // them separately
        auto& op = readv_;
        auto& sub_op = sub_req.readv_;
        sub_op = {
            .op = op.op,
            .nowait_works = op.nowait_works,
            .fd = op.fd,
            .pos = op.pos + pos,
            .iovec = iov.data(),
            .iov_len = iov.size(),
        };
        return sub_req;
    }
};

}  // namespace qstorage::io