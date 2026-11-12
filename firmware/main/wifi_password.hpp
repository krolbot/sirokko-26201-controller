#pragma once

#include <cstddef>
#include <cstring>

namespace sirokko::wifi {
constexpr size_t kPasswordCapacity = 64;
// A separate key deliberately retires the old, automatically generated password.
// Only passwords explicitly chosen by the user enable protection after upgrade.
constexpr const char* kPasswordKey = "wifi_password";

inline bool valid_password(const char* password) {
    if (!password) return false;
    const size_t length = std::strlen(password);
    if (length == 0) return true;
    if (length < 8 || length >= kPasswordCapacity) return false;
    for (size_t i = 0; i < length; ++i) {
        const auto ch = static_cast<unsigned char>(password[i]);
        if (ch < 33 || ch > 126) return false;
    }
    return true;
}
}  // namespace sirokko::wifi
