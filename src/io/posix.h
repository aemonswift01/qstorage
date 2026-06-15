#include <errno.h>
#include <concepts>
#include <system_error>

namespace qstorage::io {
template <typename T>
    requires std::signed_integral<T>
inline void ThrowKernelError(T r) {
    if (r < 0) {
        auto ec = -r;
        if (ec == EBADF || ec == ENOTSOCK) {
            throw std::system_error(ec, std::system_category());
        }
    }
}

}  // namespace qstorage::io