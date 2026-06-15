#include <queue>  // 先进先出
#include "io_request.h"
#include "reactor.h"

namespace qstorage::io {

// io_sink = 存放排队 IO 的 “队列 / 池子”，负责攒一批 IO 再批量下发
class IoSink {

    // pending_io_request = 正在排队、还没下发磁盘的 IO 请求
    class PendingIoRequest : private IoRequest {
        // IO 提交之后，不会马上返回结果！等内核做完了，必须知道要通知谁、唤醒谁！
        IoCompletion* completion_;

       public:
        PendingIoRequest(IoRequest req, IoCompletion* completion) noexcept
            : IoRequest(req), completion_(completion) {}
    };

    // 找一个高性能的库
    std::queue<PendingIoRequest> pending_io_;

   public:
    void Submit(IoRequest req, IoCompletion* completion) noexcept {
        pending_io_.emplace(PendingIoRequest(req, completion));
    }

    template <typename Fn>
        requires std::is_invocable_r<bool, Fn, IoRequest&, IoCompletion*>::value
    size_t Drain(Fn&& consume) {
        size_t processed_count = 0;
        while (!pending_io_.empty()) {
            auto& req = pending_io_.front();
            if (!consume(req, req.completion_)) {
                break;
            }
            pending_io_.pop();
            processed_count++;
        }
        return processed_count;
    }
};

}  // namespace qstorage::io