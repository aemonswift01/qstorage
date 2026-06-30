#pragma once

#include <folly/coro/Baton.h>
#include <folly/coro/Mutex.h>
#include <folly/coro/SharedMutex.h>
#include <folly/coro/Task.h>
#include <folly/io/async/IoUringBackend.h>

namespace qstorage::infra {
template <typename T>
using Task = folly::coro::Task<T>;

using Mutex = folly::coro::Mutex;

using SharedMutex = folly::coro::SharedMutex;

using Baton = folly::coro::Baton;

using IoUringBackend = folly::IoUringBackend;
}  // namespace qstorage::infra