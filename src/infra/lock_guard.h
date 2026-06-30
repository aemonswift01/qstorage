
#pragma once
#include "infra/task.h"

namespace qstorage::infra {
class LockGuard {
   public:
    LockGuard(const LockGuard&) = delete;

    LockGuard& operator=(const LockGuard&) = delete;

    LockGuard(Mutex& m) : m_(m) {}

    ~LockGuard() { m_.unlock(); }

   private:
    Mutex& m_;
};

}  // namespace qstorage::infra