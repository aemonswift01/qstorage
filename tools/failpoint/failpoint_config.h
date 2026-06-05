
#pragma once
#include "failpoint.h"

namespace failpoint {

inline void Enable(const std::string& name, Mode mode = Mode::ALWAYS,
                   uint64_t nth = 0, double prob = 1.0) {
    auto& p = Registry::Instance().Get(name);
    p.enabled = true;
    p.mode = mode;
    p.nth = nth;
    p.probability = prob;
    p.hit = 0;
}

inline void Disable(const std::string& name) {
    Registry::Instance().Get(name).enabled = false;
}

}  // namespace failpoint
