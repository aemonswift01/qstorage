#include "infra/io_uring.h"

namespace qstorage::infra {
void CoIoUring::processComplete(IoRequest* req, ssize_t res) {
    assert(req != nullptr);
    req->res_ = res;
    req->baton_.post();
}
}  // namespace qstorage::infra
