#pragma once

#include <cstddef>
#include <string>

namespace qstorage::tools {
enum class ErrorCode : size_t {
    OK = 0,
    MallocErr = 200,
};

class Error {
   public:
    Error(std::string msg, ErrorCode code)
        : msg_(std::move(msg)), code_(code) {}

    Error() : Error("", ErrorCode::OK) {}

    std::string& msg() { return msg_; }

    ErrorCode& code() { return code_; }

   private:
    std::string msg_;
    ErrorCode code_;
};
}  // namespace qstorage::tools