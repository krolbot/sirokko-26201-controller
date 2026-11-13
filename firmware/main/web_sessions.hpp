#pragma once
#include "web_protocol.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <string_view>

namespace sirokko::web_protocol {
struct Session {
    char token[33]{};
    uint64_t expires_ms{};
    ChallengeBinding challenge{};
    ReplayCache replays{};
};

// Owned by the HTTP server task. Active clients are never silently evicted.
class Sessions {
  public:
    static constexpr size_t capacity = 8;
    static constexpr uint64_t lifetime_ms = 15ULL * 60 * 1000;
    Session *find(std::string_view cookie, uint64_t now) {
        std::string_view token;
        while (!cookie.empty()) {
            const auto end = cookie.find(';');
            auto part = cookie.substr(0, end);
            while (!part.empty() && part.front() == ' ')
                part.remove_prefix(1);
            constexpr std::string_view name = "sirokko_session=";
            if (part.substr(0, name.size()) == name) {
                if (!token.empty())
                    return nullptr; // Ambiguous duplicate cookie.
                token = part.substr(name.size());
            }
            if (end == std::string_view::npos)
                break;
            cookie.remove_prefix(end + 1);
        }
        if (token.size() != 32 || now > std::numeric_limits<uint64_t>::max() - lifetime_ms)
            return nullptr;
        for (auto &session : sessions_) {
            if (!session.token[0] || now >= session.expires_ms)
                continue;
            unsigned difference = 0;
            for (size_t i = 0; i < token.size(); ++i)
                difference |= token[i] ^ session.token[i];
            if (difference == 0) {
                session.expires_ms = now + lifetime_ms;
                return &session;
            }
        }
        return nullptr;
    }
    Session *create(const char *token, uint64_t now) {
        if (!token || std::strlen(token) != 32 || now > std::numeric_limits<uint64_t>::max() - lifetime_ms)
            return nullptr;
        for (const auto &session : sessions_)
            if (session.token[0] && now < session.expires_ms && std::strcmp(session.token, token) == 0)
                return nullptr;
        for (auto &session : sessions_) {
            if (session.token[0] && now < session.expires_ms)
                continue;
            session = {};
            std::memcpy(session.token, token, sizeof(session.token));
            session.expires_ms = now + lifetime_ms;
            return &session;
        }
        return nullptr;
    }

  private:
    std::array<Session, capacity> sessions_{};
};
} // namespace sirokko::web_protocol
