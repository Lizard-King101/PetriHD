#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace phd {

// 64-bit FNV-1a, as in Petri dc03377 (World::fingerprint, core/checkpoint.cpp).
// Integers are fed least significant byte first, so the hash is the same on
// any platform.
class Fnv1a {
public:
    template <class T> void add(T value) {
        static_assert(std::is_integral<T>::value, "fingerprints take integers");
        using U = typename std::make_unsigned<T>::type;
        const auto bits = static_cast<U>(value);
        for (unsigned byte = 0; byte < sizeof(T); ++byte) {
            step(static_cast<std::uint8_t>(std::uint64_t(bits) >> (8 * byte)));
        }
    }

    void bytes(const std::uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            step(data[i]);
        }
    }

    std::uint64_t value() const { return hash_; }

private:
    void step(std::uint8_t byte) {
        hash_ ^= byte;
        hash_ *= 1099511628211ull;
    }

    std::uint64_t hash_ = 14695981039346656037ull;
};

} // namespace phd
