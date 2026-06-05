
#pragma once
#include "failpoint_config.h"

namespace failpoint {

class ScopedFailPoint {
   public:
    ScopedFailPoint(const std::string& name, Mode mode = Mode::ALWAYS,
                    uint64_t nth = 0, double prob = 1.0)
        : name_(name) {
        Enable(name_, mode, nth, prob);
    }

    ~ScopedFailPoint() { Disable(name_); }

   private:
    std::string name_;
};

}  // namespace failpoint
