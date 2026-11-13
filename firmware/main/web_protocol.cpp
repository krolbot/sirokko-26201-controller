#include "web_protocol.hpp"
#include "envelope_fields.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

namespace sirokko::web_protocol {
namespace {

constexpr uint32_t kAction = 1U << 0;
constexpr uint32_t kCommandId = 1U << 1;
constexpr uint32_t kDelay = 1U << 2;
constexpr uint32_t kSettings = 1U << 3;
constexpr uint32_t kRevision = 1U << 4;
constexpr uint32_t kUploadSize = 1U << 5;
constexpr uint32_t kUploadSha256 = 1U << 6;
constexpr uint32_t kGrant = 1U << 7;
constexpr uint32_t kEnvelope = 1U << 8;

class Cursor {
  public:
    Cursor(const char *begin, size_t size) : current_(begin), end_(begin ? begin + size : nullptr) {}

    bool valid() const { return current_ && end_; }

    void skip_space() {
        while (current_ < end_ &&
               (*current_ == ' ' || *current_ == '\t' || *current_ == '\r' || *current_ == '\n')) {
            ++current_;
        }
    }

    bool take(char expected) {
        skip_space();
        if (current_ >= end_ || *current_ != expected)
            return false;
        ++current_;
        return true;
    }

    bool at_end() {
        skip_space();
        return current_ == end_;
    }

    const char *current() const { return current_; }
    const char *end() const { return end_; }
    void advance() {
        if (current_ < end_)
            ++current_;
    }
    void set_current(const char *position) { current_ = position; }

  private:
    const char *current_{};
    const char *end_{};
};

bool ascii_printable(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return byte >= 0x20U && byte <= 0x7EU;
}

ParseError parse_string(Cursor *cursor, char *output, size_t capacity) {
    if (!cursor || !output || capacity == 0)
        return ParseError::INVALID_JSON;
    cursor->skip_space();
    if (cursor->current() >= cursor->end() || *cursor->current() != '"')
        return ParseError::FIELD_TYPE;
    cursor->advance();
    size_t length = 0;
    while (cursor->current() < cursor->end() && *cursor->current() != '"') {
        const char value = *cursor->current();
        if (!ascii_printable(value) || value == '\\' || length + 1 >= capacity)
            return ParseError::FIELD_VALUE;
        output[length++] = value;
        cursor->advance();
    }
    if (cursor->current() >= cursor->end())
        return ParseError::INVALID_JSON;
    cursor->advance();
    output[length] = 0;
    return ParseError::OK;
}

ParseError parse_u64(Cursor *cursor, uint64_t *output) {
    if (!cursor || !output)
        return ParseError::INVALID_JSON;
    cursor->skip_space();
    if (cursor->current() >= cursor->end())
        return ParseError::INVALID_JSON;
    if (*cursor->current() == '-')
        return ParseError::FIELD_VALUE;
    if (*cursor->current() < '0' || *cursor->current() > '9')
        return ParseError::FIELD_TYPE;
    uint64_t value = 0;
    bool leading_zero = *cursor->current() == '0';
    while (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9') {
        const uint8_t digit = static_cast<uint8_t>(*cursor->current() - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10U)
            return ParseError::FIELD_VALUE;
        value = value * 10U + digit;
        cursor->advance();
        if (leading_zero && cursor->current() < cursor->end() && *cursor->current() >= '0' &&
            *cursor->current() <= '9') {
            return ParseError::FIELD_VALUE;
        }
    }
    *output = value;
    return ParseError::OK;
}

ParseError parse_i32(Cursor *cursor, int32_t *output) {
    if (!cursor || !output)
        return ParseError::INVALID_JSON;
    cursor->skip_space();
    if (cursor->current() >= cursor->end())
        return ParseError::INVALID_JSON;
    bool negative = false;
    if (*cursor->current() == '-') {
        negative = true;
        cursor->advance();
    }
    if (cursor->current() >= cursor->end() || *cursor->current() < '0' || *cursor->current() > '9') {
        return ParseError::FIELD_TYPE;
    }
    uint64_t magnitude = 0;
    bool leading_zero = *cursor->current() == '0';
    while (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9') {
        const uint8_t digit = static_cast<uint8_t>(*cursor->current() - '0');
        if (magnitude > (static_cast<uint64_t>(INT32_MAX) + 1U - digit) / 10U)
            return ParseError::FIELD_VALUE;
        magnitude = magnitude * 10U + digit;
        cursor->advance();
        if (leading_zero && cursor->current() < cursor->end() && *cursor->current() >= '0' &&
            *cursor->current() <= '9') {
            return ParseError::FIELD_VALUE;
        }
    }
    if ((!negative && magnitude > static_cast<uint64_t>(INT32_MAX)) ||
        (negative && magnitude > static_cast<uint64_t>(INT32_MAX) + 1U))
        return ParseError::FIELD_VALUE;
    if (negative) {
        *output =
            magnitude == static_cast<uint64_t>(INT32_MAX) + 1U ? INT32_MIN : -static_cast<int32_t>(magnitude);
    } else {
        *output = static_cast<int32_t>(magnitude);
    }
    return ParseError::OK;
}

ParseError parse_u16(Cursor *cursor, uint16_t *output) {
    uint64_t value = 0;
    const ParseError result = parse_u64(cursor, &value);
    if (result != ParseError::OK)
        return result;
    if (value > std::numeric_limits<uint16_t>::max())
        return ParseError::FIELD_VALUE;
    *output = static_cast<uint16_t>(value);
    return ParseError::OK;
}

ParseError parse_double(Cursor *cursor, double *output) {
    if (!cursor || !output)
        return ParseError::INVALID_JSON;
    cursor->skip_space();
    const char *begin = cursor->current();
    if (begin >= cursor->end())
        return ParseError::INVALID_JSON;
    if (*cursor->current() == '-')
        cursor->advance();
    if (cursor->current() >= cursor->end() || *cursor->current() < '0' || *cursor->current() > '9') {
        return ParseError::FIELD_TYPE;
    }
    if (*cursor->current() == '0') {
        cursor->advance();
        if (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9')
            return ParseError::FIELD_VALUE;
    } else {
        while (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9')
            cursor->advance();
    }
    if (cursor->current() < cursor->end() && *cursor->current() == '.') {
        cursor->advance();
        const char *fraction = cursor->current();
        while (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9')
            cursor->advance();
        if (fraction == cursor->current())
            return ParseError::FIELD_VALUE;
    }
    if (cursor->current() < cursor->end() && (*cursor->current() == 'e' || *cursor->current() == 'E')) {
        cursor->advance();
        if (cursor->current() < cursor->end() && (*cursor->current() == '+' || *cursor->current() == '-'))
            cursor->advance();
        const char *exponent = cursor->current();
        while (cursor->current() < cursor->end() && *cursor->current() >= '0' && *cursor->current() <= '9')
            cursor->advance();
        if (exponent == cursor->current())
            return ParseError::FIELD_VALUE;
    }
    const size_t length = static_cast<size_t>(cursor->current() - begin);
    if (length == 0 || length >= 64)
        return ParseError::FIELD_VALUE;
    char number[64]{};
    std::memcpy(number, begin, length);
    char *parsed_end = nullptr;
    const double value = std::strtod(number, &parsed_end);
    if (!parsed_end || static_cast<size_t>(parsed_end - number) != length || !std::isfinite(value))
        return ParseError::FIELD_VALUE;
    *output = value;
    return ParseError::OK;
}

int hex_value(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

ParseError parse_hex_u64(Cursor *cursor, uint64_t *output) {
    char text[17]{};
    const ParseError parsed = parse_string(cursor, text, sizeof(text));
    if (parsed != ParseError::OK)
        return parsed;
    const size_t length = std::strlen(text);
    if (length == 0 || length > 16)
        return ParseError::FIELD_VALUE;
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        const int digit = hex_value(text[i]);
        if (digit < 0)
            return ParseError::FIELD_VALUE;
        value = (value << 4U) | static_cast<uint8_t>(digit);
    }
    *output = value;
    return ParseError::OK;
}

ParseError parse_hex_digest(Cursor *cursor, Digest *output) {
    char text[65]{};
    const ParseError parsed = parse_string(cursor, text, sizeof(text));
    if (parsed != ParseError::OK)
        return parsed;
    if (std::strlen(text) != 64)
        return ParseError::FIELD_VALUE;
    for (size_t i = 0; i < output->size(); ++i) {
        const int high = hex_value(text[i * 2]);
        const int low = hex_value(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            return ParseError::FIELD_VALUE;
        (*output)[i] = static_cast<uint8_t>((high << 4U) | low);
    }
    return ParseError::OK;
}

bool equals(const char *left, const char *right) {
    return left && right && std::strcmp(left, right) == 0;
}

bool parse_action_text(const char *text, sirokko::Action *action) {
    if (equals(text, "START"))
        *action = sirokko::Action::START;
    else if (equals(text, "RESET_FAULT"))
        *action = sirokko::Action::RESET_FAULT;
    else if (equals(text, "SETTINGS"))
        *action = sirokko::Action::SETTINGS;
    else if (equals(text, "OTA"))
        *action = sirokko::Action::OTA;
    else if (equals(text, "CALIBRATE"))
        *action = sirokko::Action::CALIBRATE;
    else if (equals(text, "MODEL_REVISION"))
        *action = sirokko::Action::MODEL_REVISION;
    else if (equals(text, "TIMER"))
        *action = sirokko::Action::TIMER;
    else if (equals(text, "PROFILE"))
        *action = sirokko::Action::PROFILE;

    else
        return false;
    return true;
}

ParseError parse_mode(Cursor *cursor, sirokko::Mode *mode) {
    char text[16]{};
    const ParseError parsed = parse_string(cursor, text, sizeof(text));
    if (parsed != ParseError::OK)
        return parsed;
    if (equals(text, "TEMPERATURE")) {
        *mode = sirokko::Mode::TEMPERATURE;
        return ParseError::OK;
    }
    if (equals(text, "MANUAL")) {
        *mode = sirokko::Mode::MANUAL;
        return ParseError::OK;
    }
    return ParseError::FIELD_VALUE;
}

ParseError parse_settings(Cursor *cursor, sirokko::Settings *settings) {
    if (!cursor || !settings || !cursor->take('{'))
        return ParseError::FIELD_TYPE;
    constexpr uint32_t kSetpoint = 1U << 0;
    constexpr uint32_t kDuration = 1U << 1;
    constexpr uint32_t kMode = 1U << 2;
    constexpr uint32_t kManual = 1U << 3;
    constexpr uint32_t kSlewUp = 1U << 4;
    constexpr uint32_t kSlewDown = 1U << 5;
    constexpr uint32_t kRequired = kSetpoint | kDuration | kMode | kManual | kSlewUp | kSlewDown;
    uint32_t seen = 0;
    bool have_member = false;
    while (true) {
        cursor->skip_space();
        if (have_member) {
            if (cursor->take('}'))
                break;
            if (!cursor->take(','))
                return ParseError::INVALID_JSON;
            cursor->skip_space();
            if (cursor->current() >= cursor->end() || *cursor->current() == '}')
                return ParseError::INVALID_JSON;
        } else if (cursor->take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[32]{};
        ParseError result = parse_string(cursor, key, sizeof(key));
        if (result != ParseError::OK)
            return result;
        if (!cursor->take(':'))
            return ParseError::INVALID_JSON;
        uint32_t bit = 0;
        if (equals(key, "room_setpoint_milli_c")) {
            bit = kSetpoint;
            result = parse_i32(cursor, &settings->room_setpoint_milli_c);
        } else if (equals(key, "duration_ms")) {
            bit = kDuration;
            result = parse_u64(cursor, &settings->duration_ms);
        } else if (equals(key, "mode")) {
            bit = kMode;
            result = parse_mode(cursor, &settings->mode);
        } else if (equals(key, "manual_percent")) {
            bit = kManual;
            uint64_t manual_percent = 0;
            result = parse_u64(cursor, &manual_percent);
            if (result == ParseError::OK && manual_percent <= std::numeric_limits<uint8_t>::max()) {
                settings->manual_percent = static_cast<uint8_t>(manual_percent);
            } else if (result == ParseError::OK) {
                result = ParseError::FIELD_VALUE;
            }
        } else if (equals(key, "slew_up_per_s")) {
            bit = kSlewUp;
            result = parse_double(cursor, &settings->slew_up_per_s);
        } else if (equals(key, "slew_down_per_s")) {
            bit = kSlewDown;
            result = parse_double(cursor, &settings->slew_down_per_s);
        } else {
            return ParseError::UNKNOWN_FIELD;
        }
        if ((seen & bit) != 0U)
            return ParseError::DUPLICATE_FIELD;
        if (result != ParseError::OK)
            return result;
        seen |= bit;
        have_member = true;
    }
    return seen == kRequired ? ParseError::OK : ParseError::MISSING_FIELD;
}

ParseError parse_envelope(Cursor *cursor, sirokko::SafetyEnvelope *profile) {
    if (!cursor->take('{'))
        return ParseError::FIELD_TYPE;
    uint32_t seen = 0, required = 0;
    bool have_member = false;
    while (true) {
        if (have_member) {
            if (cursor->take('}'))
                break;
            if (!cursor->take(','))
                return ParseError::INVALID_JSON;
        } else if (cursor->take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[64]{};
        auto parsed = parse_string(cursor, key, sizeof(key));
        if (parsed != ParseError::OK)
            return parsed;
        if (!cursor->take(':'))
            return ParseError::INVALID_JSON;
        parsed = ParseError::UNKNOWN_FIELD;
        unsigned index = 0;
        envelope_fields(*profile, [&](const char *name, auto &value) {
            const auto bit = 1U << index++;
            required |= bit;
            if (!equals(key, name))
                return;
            if (seen & bit) {
                parsed = ParseError::DUPLICATE_FIELD;
                return;
            }
            seen |= bit;
            using T = std::remove_reference_t<decltype(value)>;
            if constexpr (std::is_same_v<T, uint64_t>)
                parsed = parse_u64(cursor, &value);
            else if constexpr (std::is_same_v<T, int32_t>)
                parsed = parse_i32(cursor, &value);
            else
                parsed = parse_u16(cursor, &value);
        });
        if (parsed != ParseError::OK)
            return parsed;
        have_member = true;
    }
    if (seen != required)
        return ParseError::MISSING_FIELD;
    profile->provisioned = true;
    return ParseError::OK;
}

ParseError parse_revision(Cursor *cursor, sirokko::LearnedModelRevision *revision) {
    if (!cursor || !revision || !cursor->take('{'))
        return ParseError::FIELD_TYPE;
    enum Bits : uint32_t {
        EXPECTED = 1U << 0,
        MINIMUM = 1U << 1,
        MAXIMUM = 1U << 2,
        UP = 1U << 3,
        DOWN = 1U << 4
    };
    constexpr uint32_t kRequired = EXPECTED | MINIMUM | MAXIMUM | UP | DOWN;
    uint32_t seen = 0;
    bool have_member = false;
    while (true) {
        cursor->skip_space();
        if (have_member) {
            if (cursor->take('}'))
                break;
            if (!cursor->take(','))
                return ParseError::INVALID_JSON;
            cursor->skip_space();
            if (cursor->current() >= cursor->end() || *cursor->current() == '}')
                return ParseError::INVALID_JSON;
        } else if (cursor->take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[32]{};
        ParseError result = parse_string(cursor, key, sizeof(key));
        if (result != ParseError::OK)
            return result;
        if (!cursor->take(':'))
            return ParseError::INVALID_JSON;
        uint32_t bit = 0;
        if (equals(key, "expected_revision")) {
            bit = EXPECTED;
            uint64_t value{};
            result = parse_u64(cursor, &value);
            if (result == ParseError::OK && value <= UINT32_MAX)
                revision->expected_revision = static_cast<uint32_t>(value);
            else if (result == ParseError::OK)
                result = ParseError::FIELD_VALUE;
        } else if (equals(key, "minimum_speed_code")) {
            bit = MINIMUM;
            result = parse_u16(cursor, &revision->minimum_speed_code);
        } else if (equals(key, "maximum_speed_code")) {
            bit = MAXIMUM;
            result = parse_u16(cursor, &revision->maximum_speed_code);
        } else if (equals(key, "slew_up_per_s")) {
            bit = UP;
            result = parse_double(cursor, &revision->slew_up_per_s);
        } else if (equals(key, "slew_down_per_s")) {
            bit = DOWN;
            result = parse_double(cursor, &revision->slew_down_per_s);
        } else
            return ParseError::UNKNOWN_FIELD;
        if ((seen & bit) != 0U)
            return ParseError::DUPLICATE_FIELD;
        if (result != ParseError::OK)
            return result;
        seen |= bit;
        have_member = true;
    }
    return seen == kRequired ? ParseError::OK : ParseError::MISSING_FIELD;
}

ParseError parse_grant(Cursor *cursor, GrantFields *grant) {
    if (!cursor || !grant || !cursor->take('{'))
        return ParseError::FIELD_TYPE;
    constexpr uint32_t kToken = 1U << 0;
    constexpr uint32_t kVersion = 1U << 1;
    constexpr uint32_t kExpires = 1U << 2;
    uint32_t seen = 0;
    bool have_member = false;
    while (true) {
        cursor->skip_space();
        if (have_member) {
            if (cursor->take('}'))
                break;
            if (!cursor->take(','))
                return ParseError::INVALID_JSON;
            cursor->skip_space();
            if (cursor->current() >= cursor->end() || *cursor->current() == '}')
                return ParseError::INVALID_JSON;
        } else if (cursor->take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[20]{};
        ParseError result = parse_string(cursor, key, sizeof(key));
        if (result != ParseError::OK)
            return result;
        if (!cursor->take(':'))
            return ParseError::INVALID_JSON;
        uint32_t bit = 0;
        if (equals(key, "token")) {
            bit = kToken;
            result = parse_hex_u64(cursor, &grant->token);
        } else if (equals(key, "state_version")) {
            bit = kVersion;
            result = parse_hex_u64(cursor, &grant->state_version);
        } else if (equals(key, "expires_ms")) {
            bit = kExpires;
            result = parse_hex_u64(cursor, &grant->expires_ms);
        } else
            return ParseError::UNKNOWN_FIELD;
        if ((seen & bit) != 0U)
            return ParseError::DUPLICATE_FIELD;
        if (result != ParseError::OK)
            return result;
        seen |= bit;
        have_member = true;
    }
    return seen == (kToken | kVersion | kExpires) ? ParseError::OK : ParseError::MISSING_FIELD;
}

class Sha256 {
  public:
    Sha256() { reset(); }

    void update(const uint8_t *bytes, size_t size) {
        if (!bytes || size == 0)
            return;
        total_bits_ += static_cast<uint64_t>(size) * 8U;
        for (size_t i = 0; i < size; ++i) {
            block_[block_size_++] = bytes[i];
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    Digest finish() {
        const uint64_t original_bits = total_bits_;
        uint8_t one = 0x80U;
        update(&one, 1);
        uint8_t zero = 0;
        while (block_size_ != 56U)
            update(&zero, 1);
        uint8_t encoded_length[8]{};
        for (size_t i = 0; i < sizeof(encoded_length); ++i) {
            encoded_length[sizeof(encoded_length) - 1 - i] = static_cast<uint8_t>(original_bits >> (i * 8U));
        }
        update(encoded_length, sizeof(encoded_length));
        Digest result{};
        for (size_t i = 0; i < state_.size(); ++i) {
            result[i * 4] = static_cast<uint8_t>(state_[i] >> 24U);
            result[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16U);
            result[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8U);
            result[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        return result;
    }

  private:
    static uint32_t rotate_right(uint32_t value, uint32_t bits) {
        return (value >> bits) | (value << (32U - bits));
    }

    void reset() {
        state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                  0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        block_.fill(0);
        block_size_ = 0;
        total_bits_ = 0;
    }

    void transform() {
        constexpr std::array<uint32_t, 64> kRound{
            {0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
             0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
             0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
             0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
             0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
             0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
             0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
             0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
             0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
             0xc67178f2U}};
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(block_[i * 4]) << 24U) |
                       (static_cast<uint32_t>(block_[i * 4 + 1]) << 16U) |
                       (static_cast<uint32_t>(block_[i * 4 + 2]) << 8U) |
                       static_cast<uint32_t>(block_[i * 4 + 3]);
        }
        for (size_t i = 16; i < words.size(); ++i) {
            const uint32_t s0 =
                rotate_right(words[i - 15], 7U) ^ rotate_right(words[i - 15], 18U) ^ (words[i - 15] >> 3U);
            const uint32_t s1 =
                rotate_right(words[i - 2], 17U) ^ rotate_right(words[i - 2], 19U) ^ (words[i - 2] >> 10U);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t s1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choice + kRound[i] + words[i];
            const uint32_t s0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> block_{};
    size_t block_size_{};
    uint64_t total_bits_{};
};

void hash_u8(Sha256 *hash, uint8_t value) {
    hash->update(&value, 1);
}

void hash_u32(Sha256 *hash, uint32_t value) {
    const uint8_t bytes[]{static_cast<uint8_t>(value >> 24U), static_cast<uint8_t>(value >> 16U),
                          static_cast<uint8_t>(value >> 8U), static_cast<uint8_t>(value)};
    hash->update(bytes, sizeof(bytes));
}

void hash_u64(Sha256 *hash, uint64_t value) {
    uint8_t bytes[8]{};
    for (size_t i = 0; i < sizeof(bytes); ++i)
        bytes[sizeof(bytes) - 1 - i] = static_cast<uint8_t>(value >> (i * 8U));
    hash->update(bytes, sizeof(bytes));
}

void hash_i32(Sha256 *hash, int32_t value) {
    hash_u32(hash, static_cast<uint32_t>(value));
}

void hash_double(Sha256 *hash, double value) {
    static_assert(sizeof(double) == sizeof(uint64_t));
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_u64(hash, bits);
}

Digest digest_command(const Command &command) {
    constexpr char kDomain[] = "sirokko-web-command-v1";
    Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t *>(kDomain), sizeof(kDomain) - 1);
    hash_u8(&hash, static_cast<uint8_t>(command.action));
    hash_u64(&hash, command.command_id);
    switch (command.action) {
    case sirokko::Action::TIMER:
        hash_u64(&hash, command.delay_ms);
        break;
    case sirokko::Action::SETTINGS:
        hash_i32(&hash, command.settings.room_setpoint_milli_c);
        hash_u64(&hash, command.settings.duration_ms);
        hash_u8(&hash, static_cast<uint8_t>(command.settings.mode));
        hash_u8(&hash, command.settings.manual_percent);
        hash_double(&hash, command.settings.slew_up_per_s);
        hash_double(&hash, command.settings.slew_down_per_s);
        break;
    case sirokko::Action::MODEL_REVISION:
        hash_u32(&hash, command.revision.expected_revision);
        hash_u32(&hash, command.revision.minimum_speed_code);
        hash_u32(&hash, command.revision.maximum_speed_code);
        hash_double(&hash, command.revision.slew_up_per_s);
        hash_double(&hash, command.revision.slew_down_per_s);
        break;
    case sirokko::Action::PROFILE:
        envelope_fields(command.envelope,
                        [&](const char *, auto value) { hash_u64(&hash, static_cast<uint64_t>(value)); });
        break;

    case sirokko::Action::OTA:
        hash_u64(&hash, command.upload_size);
        hash.update(command.upload_sha256.data(), command.upload_sha256.size());
        break;
    case sirokko::Action::START:
    case sirokko::Action::RESET_FAULT:
    case sirokko::Action::CALIBRATE:
        break;
    }
    return hash.finish();
}

ParseError parse_request(const char *text, size_t size, bool command_submission, Command *output) {
    if (!text || size == 0 || !output)
        return ParseError::INVALID_JSON;
    Cursor cursor(text, size);
    if (!cursor.valid() || !cursor.take('{'))
        return ParseError::INVALID_JSON;
    Command command{};
    uint32_t seen = 0;
    bool have_member = false;
    char action_text[24]{};
    while (true) {
        cursor.skip_space();
        if (have_member) {
            if (cursor.take('}'))
                break;
            if (!cursor.take(','))
                return ParseError::INVALID_JSON;
            cursor.skip_space();
            if (cursor.current() >= cursor.end() || *cursor.current() == '}')
                return ParseError::INVALID_JSON;
        } else if (cursor.take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[20]{};
        ParseError result = parse_string(&cursor, key, sizeof(key));
        if (result != ParseError::OK)
            return result;
        if (!cursor.take(':'))
            return ParseError::INVALID_JSON;
        uint32_t bit = 0;
        if (equals(key, "action")) {
            bit = kAction;
            result = parse_string(&cursor, action_text, sizeof(action_text));
        } else if (equals(key, "command_id")) {
            bit = kCommandId;
            result = parse_hex_u64(&cursor, &command.command_id);
        } else if (equals(key, "delay_ms")) {
            bit = kDelay;
            result = parse_u64(&cursor, &command.delay_ms);
        } else if (equals(key, "settings")) {
            bit = kSettings;
            result = parse_settings(&cursor, &command.settings);
        } else if (equals(key, "revision")) {
            bit = kRevision;
            result = parse_revision(&cursor, &command.revision);
        } else if (equals(key, "envelope")) {
            bit = kEnvelope;
            result = parse_envelope(&cursor, &command.envelope);
        } else if (equals(key, "upload_size")) {
            bit = kUploadSize;
            result = parse_u64(&cursor, &command.upload_size);
        } else if (equals(key, "upload_sha256")) {
            bit = kUploadSha256;
            result = parse_hex_digest(&cursor, &command.upload_sha256);
        } else if (equals(key, "grant")) {
            bit = kGrant;
            result = parse_grant(&cursor, &command.grant);
            command.has_grant = result == ParseError::OK;
        } else
            return ParseError::UNKNOWN_FIELD;
        if ((seen & bit) != 0U)
            return ParseError::DUPLICATE_FIELD;
        if (result != ParseError::OK)
            return result;
        seen |= bit;
        have_member = true;
    }
    if (!cursor.at_end())
        return ParseError::INVALID_JSON;
    if ((seen & (kAction | kCommandId)) != (kAction | kCommandId) || command.command_id == 0 ||
        !parse_action_text(action_text, &command.action))
        return ParseError::ACTION;

    uint32_t required = kAction | kCommandId;
    switch (command.action) {
    case sirokko::Action::TIMER:
        required |= kDelay;
        break;
    case sirokko::Action::SETTINGS:
        required |= kSettings;
        break;
    case sirokko::Action::MODEL_REVISION:
        required |= kRevision;
        break;
    case sirokko::Action::PROFILE:
        required |= kEnvelope;
        break;

    case sirokko::Action::OTA:
        required |= kUploadSize | kUploadSha256;
        break;
    case sirokko::Action::START:
    case sirokko::Action::RESET_FAULT:
    case sirokko::Action::CALIBRATE:
        break;
    }
    if (command_submission)
        required |= kGrant;
    // STOP is intentionally not an Action enum: it is parsed by the handler before this protocol.
    if (!command_submission && (seen & kGrant) != 0U)
        return ParseError::GRANT;
    if ((seen & ~required) != 0U)
        return ParseError::UNKNOWN_FIELD;
    if ((seen & required) != required)
        return ParseError::MISSING_FIELD;
    // The signed upload has its own counted-body endpoint.  Keeping OTA out of
    // generic submissions prevents a grant from entering maintenance without
    // the package verifier and its guaranteed exit path.
    if (command_submission && command.action == sirokko::Action::OTA)
        return ParseError::OTA_UPLOAD_ONLY;
    command.payload_digest = digest_command(command);
    *output = command;
    return ParseError::OK;
}

} // namespace

Digest sha256(const uint8_t *bytes, size_t size) {
    if (!bytes && size != 0)
        return {};
    Sha256 hash;
    hash.update(bytes, size);
    return hash.finish();
}

Digest compute_payload_digest(const Command &command) {
    return digest_command(command);
}

bool digest_equal(const Digest &left, const Digest &right) {
    uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<uint8_t>(left[i] ^ right[i]);
    return difference == 0;
}

void ChallengeBinding::bind(sirokko::Action action, const GrantFields &grant, const Digest &payload_digest) {
    action_ = action;
    grant_ = grant;
    payload_digest_ = payload_digest;
    active_ = grant.token != 0 && grant.expires_ms != 0;
}

BindingResult ChallengeBinding::consume(const Command &command, uint64_t now_ms, uint64_t state_version) {
    if (!active_)
        return BindingResult::MISSING;
    if (now_ms >= grant_.expires_ms) {
        clear();
        return BindingResult::EXPIRED;
    }
    if (state_version != grant_.state_version) {
        clear();
        return BindingResult::STATE;
    }
    if (command.action != action_) {
        clear();
        return BindingResult::ACTION;
    }
    if (!digest_equal(command.payload_digest, payload_digest_)) {
        clear();
        return BindingResult::PAYLOAD;
    }
    if (!command.has_grant || command.grant.token != grant_.token ||
        command.grant.state_version != grant_.state_version ||
        command.grant.expires_ms != grant_.expires_ms) {
        clear();
        return BindingResult::GRANT;
    }
    clear();
    return BindingResult::ACCEPTED;
}

void ChallengeBinding::clear() {
    action_ = sirokko::Action::START;
    grant_ = {};
    payload_digest_ = {};
    active_ = false;
}

ReplayResult ReplayCache::lookup(uint64_t command_id, const Digest &payload_digest, bool *accepted) const {
    if (command_id == 0)
        return ReplayResult::CONFLICT;
    for (const Entry &entry : entries_) {
        if (entry.command_id != command_id)
            continue;
        if (!digest_equal(entry.payload_digest, payload_digest))
            return ReplayResult::CONFLICT;
        if (accepted)
            *accepted = entry.accepted;
        return ReplayResult::MATCH;
    }
    return ReplayResult::NEW;
}

void ReplayCache::remember(uint64_t command_id, const Digest &payload_digest, bool accepted) {
    if (command_id == 0)
        return;
    entries_[next_++ % entries_.size()] = {command_id, payload_digest, accepted};
}

uint64_t runtime_parameters_hash(const Command &command) {
    uint64_t result = 0;
    for (size_t i = 0; i < sizeof(result); ++i)
        result = (result << 8U) | command.payload_digest[i];
    return result;
}

ParseError parse_challenge_request(const char *text, size_t size, Command *command) {
    return parse_request(text, size, false, command);
}

ParseError parse_command_request(const char *text, size_t size, Command *command) {
    return parse_request(text, size, true, command);
}

ParseError parse_stop_request(const char *text, size_t size, Stop *stop) {
    if (!text || size == 0 || !stop)
        return ParseError::INVALID_JSON;
    Cursor cursor(text, size);
    if (!cursor.valid() || !cursor.take('{'))
        return ParseError::INVALID_JSON;
    uint32_t seen = 0;
    uint64_t command_id = 0;
    char action[12]{};
    bool have_member = false;
    while (true) {
        cursor.skip_space();
        if (have_member) {
            if (cursor.take('}'))
                break;
            if (!cursor.take(','))
                return ParseError::INVALID_JSON;
            cursor.skip_space();
            if (cursor.current() >= cursor.end() || *cursor.current() == '}')
                return ParseError::INVALID_JSON;
        } else if (cursor.take('}')) {
            return ParseError::MISSING_FIELD;
        }
        char key[16]{};
        ParseError result = parse_string(&cursor, key, sizeof(key));
        if (result != ParseError::OK)
            return result;
        if (!cursor.take(':'))
            return ParseError::INVALID_JSON;
        uint32_t bit = 0;
        if (equals(key, "action")) {
            bit = kAction;
            result = parse_string(&cursor, action, sizeof(action));
        } else if (equals(key, "command_id")) {
            bit = kCommandId;
            result = parse_hex_u64(&cursor, &command_id);
        } else {
            return ParseError::UNKNOWN_FIELD;
        }
        if ((seen & bit) != 0U)
            return ParseError::DUPLICATE_FIELD;
        if (result != ParseError::OK)
            return result;
        seen |= bit;
        have_member = true;
    }
    if (!cursor.at_end())
        return ParseError::INVALID_JSON;
    if (seen != (kAction | kCommandId))
        return ParseError::MISSING_FIELD;
    if (!equals(action, "STOP") || command_id == 0)
        return ParseError::ACTION;
    constexpr char kDomain[] = "sirokko-web-stop-v1";
    Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t *>(kDomain), sizeof(kDomain) - 1);
    hash_u64(&hash, command_id);
    stop->command_id = command_id;
    stop->payload_digest = hash.finish();
    return ParseError::OK;
}

const char *error_name(ParseError error) {
    switch (error) {
    case ParseError::OK:
        return "OK";
    case ParseError::INVALID_JSON:
        return "JSON";
    case ParseError::UNKNOWN_FIELD:
        return "FIELD";
    case ParseError::DUPLICATE_FIELD:
        return "DUPLICATE";
    case ParseError::FIELD_TYPE:
        return "TYPE";
    case ParseError::FIELD_VALUE:
        return "VALUE";
    case ParseError::MISSING_FIELD:
        return "MISSING";
    case ParseError::ACTION:
        return "ACTION";
    case ParseError::GRANT:
        return "GRANT";
    case ParseError::OTA_UPLOAD_ONLY:
        return "OTA_UPLOAD_ONLY";
    }
    return "PROTOCOL";
}

} // namespace sirokko::web_protocol
