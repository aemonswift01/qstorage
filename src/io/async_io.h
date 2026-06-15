#include <folly/coro/Task.h>
#include <folly/io/IOBuf.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace qstorage::io {
class AsyncIO {
   public:
    virtual ~AsyncIO() = default;

    AsyncIO(const std::string file_name, size_t queue_depth)
        : file_name_(std::move(file_name)), depth_(queue_depth) {
        std::FILE* file = std::fopen(file_name_.c_str(), "r+w+b");
        if (!file) {
            size_t err_no = errno;
            std::string err_reason = std::strerror(err_no);
            throw std::runtime_error("fopen failed. reason: " + err_reason);
        }
        file_ptr_ = std::unique_ptr<std::FILE, FileCloser>(file);
    }

    virtual folly::coro::Task<ssize_t> pwrite_async(const void* buf,
                                                    size_t size,
                                                    off_t offset) = 0;

   protected:
    struct FileCloser {
        void operator()(std::FILE* file) { std::fclose(file); }
    };

    std::string file_name_;
    uint16_t depth_;
    std::unique_ptr<std::FILE, FileCloser> file_ptr_;
};
}  // namespace qstorage::io
