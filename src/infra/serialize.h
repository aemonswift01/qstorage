#pragma once

#include <bit>
#include <concepts>
#include <cstddef>

namespace qstorage::infra {

template <typename T>
concept FloatingPointOnly = std::same_as<T, float> || std::same_as<T, double>;

template <std::integral T>
inline void SerializeLE(uint8_t* dst, T value) {
    if constexpr (std::endian::native == std::endian::big) {
        value = std::byteswap(value);
    }
    std::memcpy(dst, &value, sizeof(T));
}

template <FloatingPointOnly T>
inline void SerializeLE(uint8_t* dst, T value) {
    if constexpr (std::endian::native == std::endian::big) {
        if constexpr (sizeof(T) == 4) {
            uint32_t u32;
            std::memcpy(&u32, &value, 4);
            u32 = std::byteswap(u32);
            std::memcpy(&value, &u32, 4);
        } else if constexpr (sizeof(T) == 8) {
            uint64_t u64;
            std::memcpy(&u64, &value, 8);
            u64 = std::byteswap(u64);
            std::memcpy(&value, &u64, 8);
        }
    }
    std::memcpy(dst, &value, sizeof(T));
}

template <std::integral T>
inline T DeserializeLE(const uint8_t* src) {
    T value;
    std::memcpy(&value, src, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        return std::byteswap(value);
    }
    return value;
}

template <FloatingPointOnly T>
inline T DeserializeLE(const uint8_t* src) {
    T value;
    std::memcpy(&value, src, sizeof(T));

    if constexpr (std::endian::native == std::endian::big) {
        if constexpr (sizeof(T) == 4) {
            uint32_t u32;
            std::memcpy(&u32, &value, 4);
            u32 = std::byteswap(u32);
            std::memcpy(&value, &u32, 4);
        } else if constexpr (sizeof(T) == 8) {
            uint64_t u64;
            std::memcpy(&u64, &value, 8);
            u64 = std::byteswap(u64);
            std::memcpy(&value, &u64, 8);
        }
    }
    return value;
}
}  // namespace qstorage::infra