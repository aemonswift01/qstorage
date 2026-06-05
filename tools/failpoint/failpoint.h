
#pragma once
#include <atomic>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace failpoint {

enum class Mode { ALWAYS, ONCE, NTH, RANDOM };

struct Point {
    std::atomic<bool> enabled{false};
    std::atomic<uint64_t> hit{0};

    Mode mode{Mode::ALWAYS};
    uint64_t nth{0};
    double probability{1.0};
};

class Registry {
   public:
    static Registry& Instance() {
        static Registry inst;
        return inst;
    }

    Point& Get(const std::string& name) {
        std::lock_guard<std::mutex> lk(mu_);
        return points_[name];
    }

    bool ShouldFire(const std::string& name) {
        auto& p = Get(name);
        if (!p.enabled.load(std::memory_order_relaxed))
            return false;

        auto h = ++p.hit;

        switch (p.mode) {
            case Mode::ALWAYS:
                return true;
            case Mode::ONCE:
                return h == 1;
            case Mode::NTH:
                return p.nth > 0 && h == p.nth;
            case Mode::RANDOM:
                return dist_(rng_) < p.probability;
        }
        return false;
    }

   private:
    Registry() : rng_(std::random_device{}()) {}

    std::mutex mu_;
    std::unordered_map<std::string, Point> points_;

    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_{0.0, 1.0};
};

}  // namespace failpoint

/**
RocksDB中Failpoint的本质模型是
FailPoint(name) -> {
  enabled?
  trigger policy (always / once / nth / random)
  action
}
 */