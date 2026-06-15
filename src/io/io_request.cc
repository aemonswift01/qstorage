
#include "io_request.h"

namespace qstorage::io {

std::vector<IoRequest::IoPart> IoRequest::Split(size_t max_length) {
    auto op = OpType();
    if (op == IoRequestType::read || op == IoRequestType::write) {
        return SplitBuffer(max_length);
    }
    if (op == IoRequestType::readv || op == IoRequestType::writev) {
        return SplitIovec(max_length);
    }
    //todo log
    std::abort();
}

// read/write按照max_length进行拆分io请求。
std::vector<IoRequest::IoPart> IoRequest::SplitBuffer(size_t max_length) {
    std::vector<IoRequest::IoPart> ret;
    // the layout of _read and _write should be identical, otherwise we need to
    // have two different implementations for each of them

    const auto& op = read_;
    ret.reserve((op.size + max_length - 1) / max_length);

    size_t off = 0;
    do {
        size_t len = std::min(op.size - off, max_length);
        ret.push_back({SubReqBuffer(off, len), len, {}});
        off += len;
    } while (off < op.size);

    return ret;
}

// readv/writev按照max_length进行拆分io请求。
std::vector<IoRequest::IoPart> IoRequest::SplitIovec(size_t max_length) {
    std::vector<IoRequest::IoPart> parts;
    std::vector<iovec> vecs;
    const auto& op = readv_;
    iovec* cur = op.iovec;
    iovec* end = op.iovec + op.iov_len;
    size_t remaining = max_length;
    size_t pos = 0;
    size_t off = 0;
    while (cur != end) {
        ::iovec iov;
        iov.iov_base = reinterpret_cast<char*>(cur->iov_base) + off;
        iov.iov_len = cur->iov_len - off;

        if (iov.iov_len <= remaining) {
            remaining -= iov.iov_len;
            vecs.push_back(std::move(iov));
            cur++;
            off = 0;
            continue;
        }

        if (remaining > 0) {
            iov.iov_len = remaining;
            off += remaining;
            vecs.push_back(std::move(iov));
        }

        auto req = SubReqIovec(pos, vecs);
        parts.push_back({std::move(req), max_length, std::move(vecs)});
        pos += max_length;
        remaining = max_length;
    }

    if (vecs.size() > 0) {
        // SEASTAR_ASSERT(remaining < max_length);
        auto req = SubReqIovec(pos, vecs);
        parts.push_back(
            {std::move(req), max_length - remaining, std::move(vecs)});
    }

    return parts;
}
}  // namespace qstorage::io
