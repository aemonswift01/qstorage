#include "io_desc.h"
#include "posix.h"

#include <exception>

namespace qstorage::io {

class IoCompletion : public KernelCompletion {
    virtual void CompleteWith(ssize_t res) final override {
        if (res >= 0) {
            Complete(res);
            return;
        }
        try {
            qstorage::io::ThrowKernelError(res);
        } catch (...) {
            SetException(std::current_exception());
        }
    }

    virtual void Complete(size_t res) noexcept = 0;
    virtual void SetException(std::exception_ptr eptr) noexcept = 0;
};

}  // namespace qstorage::io