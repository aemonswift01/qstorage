#pragma once

#include <folly/io/async/IoUring.h>
#include "async_io.h"

namespace qstorage::io {

class IoUring : public AsyncIO {
   public:
    IoUring(const std::string file_name, size_t queue_depth)
        : AsyncIO(file_name, queue_depth) {}

   private:
    std::unique_ptr<>
};

}  // namespace qstorage::io