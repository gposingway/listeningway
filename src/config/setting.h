// ---------------------------------------------
// Setting<T> — declarative bounds for a tunable (ADR-0004)
//
// A single Setting<T> object is the canonical home for a value's default,
// min, max, persistence key, and tooltip. UI sliders, validators, and the
// persistence layer all consume the same declaration. The drift class of
// bug from v1 (default 0.1, validator [0.2, 3.0], slider [0.01, 1.5]) is
// structurally impossible if every site reads from the same Setting<T>.
// ---------------------------------------------
#pragma once

#include <algorithm>
#include <string_view>

namespace lw::config {

template <typename T>
struct Setting {
    T default_value{};
    T min{};
    T max{};
    std::string_view key;        ///< JSON key + UI label key
    std::string_view tooltip;    ///< overlay tooltip text

    constexpr T clamp(T candidate) const noexcept {
        return std::clamp(candidate, min, max);
    }
};

}  // namespace lw::config
