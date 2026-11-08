#include "web_protocol.hpp"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

const char kSettingsChallenge[] = R"JSON({
  "action":"SETTINGS",
  "command_id":"0011223344556677",
  "settings":{
    "room_setpoint_milli_c":20000,
    "duration_ms":3600000,
    "mode":"TEMPERATURE",
    "manual_percent":50,
    "slew_up_per_s":0.005,
    "slew_down_per_s":0.004
  }
})JSON";

const char kSettingsSubmission[] = R"JSON({
  "grant":{"token":"abc","state_version":"22","expires_ms":"1000"},
  "action":"SETTINGS",
  "command_id":"0011223344556677",
  "settings":{
    "room_setpoint_milli_c":20000,
    "duration_ms":3600000,
    "mode":"TEMPERATURE",
    "manual_percent":50,
    "slew_up_per_s":0.005,
    "slew_down_per_s":0.004
  }
})JSON";

const char kProcessChallenge[] = R"JSON({
 "action":"PROCESS_CONFIG","command_id":"7",
 "process":{
  "qualified":true,
  "recovery_purge_ms":1,"prepurge_ms":2,"preheat_ms":3,"ignition_ms":4,"verify_ms":5,"postpurge_ms":6,
  "maximum_purge_exit_exhaust_milli_c":7,"maximum_purge_exit_air_milli_c":8,
  "maximum_exhaust_milli_c":9,"maximum_air_milli_c":10,
  "minimum_ignition_rise_milli_c":11,"minimum_run_exhaust_milli_c":12,
  "purge_speed_code":13,"run_speed_code":14,"calibration_floor_speed_code":15,
  "minimum_ignition_air_rise_milli_c":16
 }
})JSON";

}  // namespace

int main() {
    using namespace sirokko::web_protocol;
    sirokko::Settings current{};

    constexpr uint8_t kSha256Abc[] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad,
    };
    const Digest abc = sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
    check(std::memcmp(abc.data(), kSha256Abc, sizeof(kSha256Abc)) == 0, "portable SHA-256 abc vector");

    Command challenge{};
    check(parse_challenge_request(kSettingsChallenge, std::strlen(kSettingsChallenge), current, &challenge) == ParseError::OK,
          "strict settings challenge parses");
    check(challenge.action == sirokko::Action::SETTINGS && challenge.command_id == 0x0011223344556677ULL,
          "challenge action and command id");
    check(challenge.settings.room_hysteresis_milli_c == current.room_hysteresis_milli_c,
          "legacy hysteresis is preserved but not supplied by UI");
    check(challenge.settings.manual_percent == 50 && challenge.settings.slew_down_per_s == 0.004,
          "settings values parse exactly");

    Command submitted{};
    check(parse_command_request(kSettingsSubmission, std::strlen(kSettingsSubmission), current, &submitted) == ParseError::OK,
          "strict settings submission parses");
    check(digest_equal(challenge.payload_digest, submitted.payload_digest), "challenge and submit full payload hashes match");
    check(submitted.has_grant && submitted.grant.token == 0xabc && submitted.grant.state_version == 0x22,
          "grant fields parse as strict hex");

    ChallengeBinding binding;
    binding.bind(challenge.action, submitted.grant, challenge.payload_digest);
    check(binding.consume(submitted, 999, 0x22) == BindingResult::ACCEPTED,
          "full digest, age, state and grant bind an accepted submit");
    check(binding.consume(submitted, 999, 0x22) == BindingResult::MISSING,
          "challenge is one-shot after consumption");

    Command altered = submitted;
    altered.settings.manual_percent = 55;
    const char kAltered[] = R"JSON({"action":"SETTINGS","command_id":"0011223344556677","grant":{"token":"abc","state_version":"22","expires_ms":"1000"},"settings":{"room_setpoint_milli_c":20000,"duration_ms":3600000,"mode":"TEMPERATURE","manual_percent":55,"slew_up_per_s":0.005,"slew_down_per_s":0.004}})JSON";
    check(parse_command_request(kAltered, std::strlen(kAltered), current, &altered) == ParseError::OK,
          "altered settings submission parses before binding rejection");
    binding.bind(challenge.action, submitted.grant, challenge.payload_digest);
    check(binding.consume(altered, 999, 0x22) == BindingResult::PAYLOAD,
          "different full payload is rejected even with a matching token");
    binding.bind(challenge.action, submitted.grant, challenge.payload_digest);
    check(binding.consume(submitted, 0x1000, 0x22) == BindingResult::EXPIRED,
          "expired challenge is rejected");
    binding.bind(challenge.action, submitted.grant, challenge.payload_digest);
    check(binding.consume(submitted, 999, 0x23) == BindingResult::STATE,
          "state-version change is rejected");

    ReplayCache replay;
    bool accepted = false;
    check(replay.lookup(submitted.command_id, submitted.payload_digest, &accepted) == ReplayResult::NEW,
          "new command id is not a replay");
    replay.remember(submitted.command_id, submitted.payload_digest, true);
    check(replay.lookup(submitted.command_id, submitted.payload_digest, &accepted) == ReplayResult::MATCH && accepted,
          "same command id and full payload returns remembered outcome");
    Digest different_digest = submitted.payload_digest;
    different_digest[0] ^= 1U;
    check(replay.lookup(submitted.command_id, different_digest, &accepted) == ReplayResult::CONFLICT,
          "same command id with different payload is rejected");

    const char kDuplicate[] = R"JSON({"action":"START","command_id":"1","command_id":"1"})JSON";
    Command parsed{};
    check(parse_challenge_request(kDuplicate, std::strlen(kDuplicate), current, &parsed) == ParseError::DUPLICATE_FIELD,
          "duplicate JSON fields are rejected");
    const char kUnknown[] = R"JSON({"action":"START","command_id":"1","extra":0})JSON";
    check(parse_challenge_request(kUnknown, std::strlen(kUnknown), current, &parsed) == ParseError::UNKNOWN_FIELD,
          "unknown JSON fields are rejected");
    const char kNoGrant[] = R"JSON({"action":"START","command_id":"1"})JSON";
    check(parse_command_request(kNoGrant, std::strlen(kNoGrant), current, &parsed) == ParseError::MISSING_FIELD,
          "non-stop submission without grant is rejected");
    const char kBadNumber[] = R"JSON({"action":"TIMER","command_id":"1","delay_ms":1.0})JSON";
    check(parse_challenge_request(kBadNumber, std::strlen(kBadNumber), current, &parsed) != ParseError::OK,
          "fractional integral delay is rejected");

    const char kOtaChallenge[] = R"JSON({"action":"OTA","command_id":"2","upload_size":1024,"upload_sha256":"0000000000000000000000000000000000000000000000000000000000000000"})JSON";
    const char kOtaSubmission[] = R"JSON({"action":"OTA","command_id":"2","upload_size":1024,"upload_sha256":"0000000000000000000000000000000000000000000000000000000000000000","grant":{"token":"abc","state_version":"22","expires_ms":"1000"}})JSON";
    check(parse_challenge_request(kOtaChallenge, std::strlen(kOtaChallenge), current, &parsed) == ParseError::OK,
          "OTA package metadata remains eligible for an upload challenge");
    check(parse_command_request(kOtaSubmission, std::strlen(kOtaSubmission), current, &parsed) == ParseError::OTA_UPLOAD_ONLY &&
          std::strcmp(error_name(ParseError::OTA_UPLOAD_ONLY), "OTA_UPLOAD_ONLY") == 0,
          "generic command submission explicitly rejects OTA before runtime routing");

    const char kArm[] = R"JSON({"action":"ARM","command_id":"30","delay_ms":60000})JSON";
    const char kCancelArm[] = R"JSON({"action":"CANCEL_ARM","command_id":"31"})JSON";
    for(const char* removed: {kArm,kCancelArm})
        check(parse_challenge_request(removed,std::strlen(removed),current,&parsed)==ParseError::ACTION,
              "removed delayed-start actions cannot obtain a public API grant");
    const char kTimer[] = R"JSON({"action":"TIMER","command_id":"32","delay_ms":0})JSON";
    check(parse_challenge_request(kTimer,std::strlen(kTimer),current,&parsed)==ParseError::OK &&
          parsed.action==sirokko::Action::TIMER && parsed.delay_ms==0,"single shutdown timer cancellation remains supported");

    Command process{};
    check(parse_challenge_request(kProcessChallenge, std::strlen(kProcessChallenge), current, &process) == ParseError::OK,
          "full process profile shape parses without invented UI defaults");
    check(process.process.minimum_ignition_air_rise_milli_c == 16, "new air-rise profile field is preserved");

    Stop stop{};
    const char kStop[] = R"JSON({"action":"STOP","command_id":"9"})JSON";
    check(parse_stop_request(kStop, std::strlen(kStop), &stop) == ParseError::OK && stop.command_id == 9,
          "STOP is strict auth-only protocol with no grant");
    const char kStopExtra[] = R"JSON({"action":"STOP","command_id":"9","delay_ms":1})JSON";
    check(parse_stop_request(kStopExtra, std::strlen(kStopExtra), &stop) == ParseError::UNKNOWN_FIELD,
          "STOP accepts no hidden action parameters");

    if (failures == 0) std::puts("PASS: web protocol strict parsing, full-payload binding, age/state/replay and STOP");
    return failures == 0 ? 0 : 1;
}
