
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <string>
#include "allocator.h"
#include "config.h"
#include "log.h"
#include "logger.h"

namespace qstorage::tools {

class Histogram final {
   public:
    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

    ~Histogram() = default;

    Histogram(Histogram&&) noexcept = default;
    Histogram& operator=(Histogram&&) noexcept = default;

    Histogram(const std::vector<uint32_t>& bucket)
        : num_buckets_(bucket.size()) {
        for (std::size_t i = 0; i < bucket.size(); i++) {
            this->data_[i] = bucket[i];
        }
    }

    void Insert(uint32_t datum) {
        int32_t lo = 0, hi = this->num_buckets_ - 1;
        while (hi > lo) {
            int mid = lo + (hi - lo) / 2;

            if (datum > this->data_[mid]) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        data_[lo + num_buckets_]++;
        if (max_ < datum) {
            max_ = datum;
        }
        total_ += datum;
        num_++;
    }

    void Merge(const Histogram& h) {
#ifdef DEBUG
        assert(num_buckets_ == h.num_buckets_);
        for (std::size_t i = 0; i < num_buckets_; i++) {
            assert(data_[i] == h.data_[i]);
        }
#endif
        if (max_ > h.max_) {
            max_ = h.max_;
        }
        total_ += h.total_;
        num_ += h.num_;
        for (std::size_t i = num_buckets_; i < h.num_buckets_ * 2; i++) {
            data_[i] += h.data_[i];
        }
    }

    void Print(std::string& name, Logger& logger) {
        LOG_INFO(logger, "histogram name:{},max:{},mean:{},count:{}\n", name,
                 max_, num_ == 0 ? 0 : total_ / num_, num_);
        for (uint32_t i = 0; i < num_buckets_; i++) {
            if (i != num_buckets_ - 1) {
                LOG_INFO(logger, "{}~{}:{}\n", data_[i], data_[i + 1],
                         data_[num_buckets_ + i]);
            } else {
                LOG_INFO(logger, "{}~+Inf:{}\n", data_[i],
                         data_[num_buckets_ + i]);
            }
        }
    }

   private:
    const int32_t num_buckets_;
    uint32_t max_ = 0;
    uint64_t num_ = 0;
    uint64_t total_ = 0;
    uint32_t data_[1];
};

template <IsAllocator Alloc>
Histogram* CreateHistogram(Alloc& a, std::vector<uint32_t>& buckets) {
    size_t n = sizeof(Histogram) + 2 * sizeof(uint32_t) * buckets.size();
    auto ptr = a.allocate(n);
    Histogram* p = new (ptr) Histogram(buckets);
    return p;
}

template <IsAllocator Alloc>
void FreeHistogram(Histogram* p, Alloc& a) {
    return a.deallocate(p);
}
}  // namespace qstorage::tools
