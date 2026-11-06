#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "sirokko/control.hpp"

namespace sirokko::web_protocol {

constexpr size_t kDigestSize = 32;
using Digest = std::array<uint8_t, kDigestSize>;

enum class ParseError : uint8_t {
    OK,
    INVALID_JSON,
    UNKNOWN_FIELD,
    DUPLICATE_FIELD,
    FIELD_TYPE,
    FIELD_VALUE,
    MISSING_FIELD,
    ACTION,
    SETTINGS,
    PROCESS,
    GRANT,
    STOP_CHALLENGE,
};

struct GrantFields {
    uint64_t token{};
    uint64_t state_version{};
    uint64_t expires_ms{};
};

// This is the complete semantic command payload. Grant fields are deliberately
// separate: they are issued only after the payload digest has been established.
struct Command {
    sirokko::Action action{sirokko::Action::START};
    uint64_t command_id{};
    uint64_t delay_ms{};
    sirokko::Settings settings{};
    sirokko::ProcessProfile process{};
    uint64_t upload_size{};
    Digest upload_sha256{};
    GrantFields grant{};
    Digest payload_digest{};
    bool has_grant{};
};

struct Stop {
    uint64_t command_id{};
    Digest payload_digest{};
};

enum class BindingResult : uint8_t { ACCEPTED, MISSING, EXPIRED, STATE, ACTION, PAYLOAD, GRANT };

// Web-local complement to the 64-bit core gate: this binds the exact SHA-256
// semantic payload to the one grant returned to the browser.
class ChallengeBinding {
public:
    void bind(sirokko::Action action, const GrantFields& grant, const Digest& payload_digest);
    BindingResult consume(const Command& command, uint64_t now_ms, uint64_t state_version);
    void clear();
private:
    sirokko::Action action_{sirokko::Action::START};
    GrantFields grant_{};
    Digest payload_digest_{};
    bool active_{};
};

enum class ReplayResult : uint8_t { NEW, MATCH, CONFLICT };

class ReplayCache {
public:
    ReplayResult lookup(uint64_t command_id, const Digest& payload_digest, bool* accepted) const;
    void remember(uint64_t command_id, const Digest& payload_digest, bool accepted);
private:
    struct Entry {
        uint64_t command_id{};
        Digest payload_digest{};
        bool accepted{};
    };
    std::array<Entry, 16> entries_{};
    size_t next_{};
};

// Strict JSON parsing. Challenge requests contain no grant. Non-STOP command
// submissions require one grant object; STOP intentionally remains auth-only.
ParseError parse_challenge_request(const char* text, size_t size, const sirokko::Settings& current,
                                   Command* command);
ParseError parse_command_request(const char* text, size_t size, const sirokko::Settings& current,
                                 Command* command);
ParseError parse_stop_request(const char* text, size_t size, Stop* stop);

// SHA-256 and semantic digest are portable so host protocol tests exercise the
// exact parser/digest code used by the ESP target.
Digest sha256(const uint8_t* bytes, size_t size);
Digest compute_payload_digest(const Command& command);
bool digest_equal(const Digest& left, const Digest& right);
uint64_t runtime_parameters_hash(const Command& command);
const char* error_name(ParseError error);

}  // namespace sirokko::web_protocol
