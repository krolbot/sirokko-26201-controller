#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "web_host_stubs/esp_fake.hpp"

// This compiles the production handler translation unit with only ESP-IDF transport/runtime
// shims. Requests below call actual handlers and the actual bounded receiver/protocol/OTA code.
#include "../main/web.cpp"

namespace {

int failures = 0;

void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<uint8_t> read_bytes(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string hex_digest(const sirokko::web_protocol::Digest& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (uint8_t byte : digest) {
        text.push_back(kHex[byte >> 4U]);
        text.push_back(kHex[byte & 0x0fU]);
    }
    return text;
}

void copy_text(char* output, size_t capacity, const char* text) {
    if (!output || capacity == 0) return;
    std::snprintf(output, capacity, "%s", text ? text : "");
}

httpd_req_t request_with_body(const std::string& body, const std::string& cookie = {}) {
    httpd_req_t request{};
    request.body = body;
    request.content_len = static_cast<int>(body.size());
    if (!cookie.empty()) request.headers["Cookie"] = cookie;
    return request;
}

std::string cookie_from_login(const httpd_req_t& request) {
    const auto found = request.response_headers.find("Set-Cookie");
    if (found == request.response_headers.end()) return {};
    const size_t separator = found->second.find(';');
    return found->second.substr(0, separator);
}

}  // namespace

namespace fake_runtime {
RuntimeView view{};
bool maintenance{};
bool accept_submit{true};
unsigned issue_calls{};
unsigned submit_calls{};
unsigned stop_calls{};
unsigned exit_maintenance_calls{};
unsigned activation_requests{};
sirokko::Action last_action{sirokko::Action::START};

void reset() {
    view = {};
    view.control.state = sirokko::State::IDLE;
    view.control.start_block = sirokko::Block::NONE;
    view.control.state_version = 0x22;
    view.control.outputs.fan_request = false;
    view.control.command_percent = 37.5;
    view.control.range_calibrated = true;
    view.control.minimum_speed_code = 100;
    view.control.maximum_speed_code = 900;
    view.control.working_span_fraction = 0.4;
    view.control.calibration_phase = "IDLE";
    view.control.calibration_reason = "NONE";
    view.settings = {};
    view.process.qualified = true;
    view.storage_ok = true;
    view.sensors.room = {21000, 1, 0, true};
    view.sensors.exhaust = {50000, 1, 0, true};
    view.sensors.air = {25000, 1, 0, true};
    view.events[0] = {1, "START", sirokko::State::IDLE, 0, 50000, 25000, 0};
    view.event_count = 1;
    maintenance = false;
    accept_submit = true;
    issue_calls = submit_calls = stop_calls = exit_maintenance_calls = 0;
    last_action = sirokko::Action::START;
}
}  // namespace fake_runtime

namespace sirokko {
bool valid_settings(const Settings&) { return true; }
bool valid_process_profile(const ProcessProfile&) { return true; }
uint64_t process_fingerprint(const ProcessProfile&) { return 0x1234; }
const char* state_name(State state) {
    switch (state) {
        case State::IDLE: return "IDLE";
        case State::PREPURGE: return "PREPURGE";
        case State::PREHEAT: return "PREHEAT";
        case State::IGNITION: return "IGNITION";
        case State::VERIFY: return "VERIFY";
        case State::RUN: return "RUN";
        case State::STOP_PURGE: return "STOP_PURGE";
        case State::FAULT_PURGE: return "FAULT_PURGE";
        case State::FAULT: return "FAULT";
        case State::MAINTENANCE: return "MAINTENANCE";
        case State::RECOVERY_PURGE: return "RECOVERY_PURGE";
    }
    return "UNKNOWN";
}
const char* block_name(Block block) {
    switch (block) {
        case Block::NONE: return "NONE";
        case Block::PROFILE_NOT_QUALIFIED: return "PROFILE_NOT_QUALIFIED";
        case Block::RECOVERY_REQUIRED: return "RECOVERY_REQUIRED";
        case Block::SENSOR_INVALID: return "SENSOR_INVALID";
        case Block::TEMPERATURE_OUT_OF_RANGE: return "TEMPERATURE_OUT_OF_RANGE";
        case Block::FAULT_LATCHED: return "FAULT_LATCHED";
        case Block::BUSY: return "BUSY";
        case Block::MAINTENANCE: return "MAINTENANCE";
        case Block::STALE_COMMAND: return "STALE_COMMAND";
    }
    return "UNKNOWN";
}
}  // namespace sirokko

bool runtime_view(RuntimeView* output) {
    if (!output) return false;
    *output = fake_runtime::view;
    output->now_ms = fake_esp::now_us / 1000U;
    return true;
}

bool runtime_issue(sirokko::Action action, uint64_t params_hash, sirokko::Grant* grant) {
    if (!grant) return false;
    ++fake_runtime::issue_calls;
    *grant = {0xabc, fake_runtime::view.control.state_version, params_hash, 0x1000, action, true};
    return true;
}

bool runtime_submit(const sirokko::Grant& grant, sirokko::Action action, uint64_t,
                    const sirokko::Settings*) {
    ++fake_runtime::submit_calls;
    fake_runtime::last_action = action;
    if (!grant.valid || !fake_runtime::accept_submit) return false;
    if (action == sirokko::Action::OTA) fake_runtime::maintenance = true;
    return true;
}

void runtime_stop(bool) { ++fake_runtime::stop_calls; }

bool runtime_configure_process(const sirokko::Grant& grant, const sirokko::ProcessProfile&) {
    ++fake_runtime::submit_calls;
    fake_runtime::last_action = sirokko::Action::PROCESS_CONFIG;
    return grant.valid && fake_runtime::accept_submit;
}

bool runtime_process_valid(const sirokko::ProcessProfile&) { return true; }
bool runtime_network_credentials(char*, size_t, char*, size_t) { return false; }
bool runtime_maintenance_safe() { return fake_runtime::maintenance; }
uint32_t runtime_maintenance_lease(){return fake_runtime::maintenance?1U:0U;}
bool runtime_activate_ota(uint32_t lease,const void* partition) {
    if(!lease || lease!=runtime_maintenance_lease() || !partition) return false;
    ++fake_runtime::activation_requests;
    return true; // only queued: the production control task owns activation/reboot
}
void runtime_exit_maintenance() { fake_runtime::maintenance = false; ++fake_runtime::exit_maintenance_calls; }

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s VALID_SOTA WRONG_IMAGE_TARGET_SOTA\n", argv[0]);
        return 2;
    }
    const std::vector<uint8_t> package = read_bytes(argv[1]);
    const std::vector<uint8_t> wrong_image_target = read_bytes(argv[2]);
    check(!package.empty() && !wrong_image_target.empty(), "signed OTA fixtures are readable");
    if (package.empty() || wrong_image_target.empty()) return 1;

    fake_esp::reset();
    fake_runtime::reset();
    copy_text(fake_esp::app_description.version, sizeof(fake_esp::app_description.version), "host-test");
    copy_text(fake_esp::app_description.project_name, sizeof(fake_esp::app_description.project_name), "sirokko_26201_controller");
    copy_text(fake_esp::partition_description.project_name, sizeof(fake_esp::partition_description.project_name), "sirokko_26201_controller");

    constexpr char kPassword[] = "12345678901234567890";
    check(web_start(kPassword), "actual web_start registers HTTP routes");
    check(fake_esp::registered_handlers.size() == 6, "all six HTTP routes are registered");

    httpd_req_t page{};
    check(page_handler(&page) == ESP_OK && page.response_body.find("СВЯЗЬ ПОТЕРЯНА") != std::string::npos &&
          page.response_body.find("signed .sota") == std::string::npos &&
          page.response_body.find(".sota") != std::string::npos,
          "actual page handler returns Russian offline self-contained UI");

    httpd_req_t unauthenticated = request_with_body("{\"action\":\"START\",\"command_id\":\"1\"}");
    check(command_handler(&unauthenticated) == ESP_OK && unauthenticated.response_status == "401 Unauthorized" &&
          fake_runtime::submit_calls == 0, "unauthenticated start is rejected before runtime submit");

    fake_esp::now_us = 1000U * 1000U;
    httpd_req_t login{};
    login.headers["X-Sirokko-Password"] = kPassword;
    check(login_handler(&login) == ESP_OK && login.response_body.find("\"OK\"") != std::string::npos,
          "actual login handler accepts the configured AP password");
    const std::string cookie = cookie_from_login(login);
    check(cookie.rfind("sirokko_session=", 0) == 0, "login creates an HttpOnly session cookie");

    httpd_req_t state = request_with_body({}, cookie);
    check(state_handler(&state) == ESP_OK && state.response_body.find("\"ru\":\"Ожидание\"") != std::string::npos &&
          state.response_body.find("\"physical_tests\":false") != std::string::npos &&
          state.response_body.find("\"kind_ru\":\"Запуск принят автоматом\"") != std::string::npos,
          "actual state handler serializes Russian state, process boundary and event journal");

    const std::string start_challenge = "{\"action\":\"START\",\"command_id\":\"1\"}";
    const unsigned issue_before_nul=fake_runtime::issue_calls;
    httpd_req_t nul_challenge=request_with_body(start_challenge+std::string(1,'\0')+"{}",cookie);
    challenge_handler(&nul_challenge);
    check(nul_challenge.response_status=="400 Bad Request" && fake_runtime::issue_calls==issue_before_nul,
          "embedded NUL challenge is rejected with no issued grant");
    httpd_req_t challenge = request_with_body(start_challenge, cookie);
    check(challenge_handler(&challenge) == ESP_OK && challenge.response_body.find("\"status\":\"CHALLENGE\"") != std::string::npos,
          "actual challenge handler issues a grant for full START payload");
    const std::string start_submit =
        "{\"action\":\"START\",\"command_id\":\"1\",\"grant\":{\"token\":\"abc\",\"state_version\":\"22\",\"expires_ms\":\"1000\"}}";
    httpd_req_t nul_command=request_with_body(start_submit+std::string(1,'\0')+"{}",cookie);
    command_handler(&nul_command);
    check(nul_command.response_status=="400 Bad Request" && fake_runtime::submit_calls==0,
          "embedded NUL command is rejected with no prefix submission");
    httpd_req_t command = request_with_body(start_submit, cookie);
    check(command_handler(&command) == ESP_OK && command.response_body.find("\"QUEUED\"") != std::string::npos &&
          fake_runtime::submit_calls == 1 && fake_runtime::last_action == sirokko::Action::START,
          "actual command handler binds grant and forwards only accepted START");
    httpd_req_t replay = request_with_body(start_submit, cookie);
    check(command_handler(&replay) == ESP_OK && replay.response_body.find("\"replay\":true") != std::string::npos &&
          fake_runtime::submit_calls == 1, "actual handler suppresses duplicate START submission");

    httpd_req_t stop = request_with_body("{\"action\":\"STOP\",\"command_id\":\"2\"}", cookie);
    check(command_handler(&stop) == ESP_OK && stop.response_body.find("STOP_REQUESTED") != std::string::npos &&
          fake_runtime::stop_calls == 1, "STOP takes the separate immediate route without a grant");
    httpd_req_t stop_replay = request_with_body("{\"action\":\"STOP\",\"command_id\":\"2\"}", cookie);
    check(command_handler(&stop_replay) == ESP_OK && stop_replay.response_body.find("\"replay\":true") != std::string::npos &&
          fake_runtime::stop_calls == 1, "duplicate STOP is idempotent and does not call runtime twice");

    const auto package_digest = sirokko::web_protocol::sha256(package.data(), package.size());
    const std::string package_digest_hex = hex_digest(package_digest);
    const std::string ota_challenge = "{\"action\":\"OTA\",\"command_id\":\"a\",\"upload_size\":" +
        std::to_string(package.size()) + ",\"upload_sha256\":\"" + package_digest_hex + "\"}";
    httpd_req_t ota_grant = request_with_body(ota_challenge, cookie);
    check(challenge_handler(&ota_grant) == ESP_OK && ota_grant.response_body.find("CHALLENGE") != std::string::npos,
          "actual OTA challenge binds size and SHA-256 before upload");
    httpd_req_t ota = request_with_body(std::string(reinterpret_cast<const char*>(package.data()), package.size()), cookie);
    ota.headers["X-Sirokko-Token"] = "abc";
    ota.headers["X-Sirokko-State-Version"] = "22";
    ota.headers["X-Sirokko-Expires"] = "1000";
    ota.headers["X-Sirokko-Command-Id"] = "a";
    ota.headers["X-Sirokko-Upload-Size"] = std::to_string(package.size());
    ota.headers["X-Sirokko-Upload-SHA256"] = package_digest_hex;
    check(ota_handler(&ota) == ESP_OK && ota.response_body.find("VERIFIED_ACTIVATION_QUEUED") != std::string::npos &&
          fake_runtime::activation_requests==1 && !fake_esp::boot_partition && !fake_esp::restart_requested &&
          !fake_esp::ota_bytes.empty(), "actual OTA handler verifies and queues activation, never selects partition or reboots");

    fake_esp::boot_partition = nullptr;
    fake_esp::restart_requested = false;
    std::string lost_payload=ota_challenge;
    lost_payload.replace(lost_payload.find("\"a\""),3,"\"e\"");
    httpd_req_t lost_grant=request_with_body(lost_payload,cookie);
    challenge_handler(&lost_grant);
    httpd_req_t lost_ota=ota;
    lost_ota.body_offset=0;lost_ota.response_body.clear();
    lost_ota.headers["X-Sirokko-Command-Id"]="e";
    fake_esp::ota_end_hook=+[](){fake_runtime::maintenance=false;};
    ota_handler(&lost_ota);
    fake_esp::ota_end_hook=nullptr;
    check(lost_ota.response_status=="409 Conflict" && fake_runtime::activation_requests==1 && !fake_esp::boot_partition && !fake_esp::restart_requested,
          "maintenance loss inside esp_ota_end forbids activation and restart");
    fake_esp::boot_partition=nullptr;fake_esp::restart_requested=false;
    const auto bad_digest = sirokko::web_protocol::sha256(wrong_image_target.data(), wrong_image_target.size());
    const std::string bad_digest_hex = hex_digest(bad_digest);
    const std::string bad_challenge = "{\"action\":\"OTA\",\"command_id\":\"b\",\"upload_size\":" +
        std::to_string(wrong_image_target.size()) + ",\"upload_sha256\":\"" + bad_digest_hex + "\"}";
    httpd_req_t bad_grant = request_with_body(bad_challenge, cookie);
    check(challenge_handler(&bad_grant) == ESP_OK, "target-mismatch OTA gets a separately bound challenge");
    httpd_req_t bad_ota = request_with_body(
        std::string(reinterpret_cast<const char*>(wrong_image_target.data()), wrong_image_target.size()), cookie);
    bad_ota.headers["X-Sirokko-Token"] = "abc";
    bad_ota.headers["X-Sirokko-State-Version"] = "22";
    bad_ota.headers["X-Sirokko-Expires"] = "1000";
    bad_ota.headers["X-Sirokko-Command-Id"] = "b";
    bad_ota.headers["X-Sirokko-Upload-Size"] = std::to_string(wrong_image_target.size());
    bad_ota.headers["X-Sirokko-Upload-SHA256"] = bad_digest_hex;
    const int aborts_before = fake_esp::ota_abort_count;
    check(ota_handler(&bad_ota) == ESP_OK && bad_ota.response_status == "409 Conflict" &&
          bad_ota.response_body.find("TARGET") != std::string::npos && fake_esp::boot_partition == nullptr &&
          !fake_esp::restart_requested && fake_esp::ota_abort_count == aborts_before + 1 &&
          fake_runtime::exit_maintenance_calls > 0,
          "wrong actual image chip target aborts OTA and never activates a slot");

    fake_esp::boot_partition = nullptr;
    fake_esp::restart_requested = false;
    const std::string timeout_challenge = "{\"action\":\"OTA\",\"command_id\":\"c\",\"upload_size\":" +
        std::to_string(package.size()) + ",\"upload_sha256\":\"" + package_digest_hex + "\"}";
    httpd_req_t timeout_grant = request_with_body(timeout_challenge, cookie);
    check(challenge_handler(&timeout_grant) == ESP_OK, "interrupted OTA gets a separately bound challenge");
    httpd_req_t timeout_ota = request_with_body(std::string(reinterpret_cast<const char*>(package.data()), package.size()), cookie);
    timeout_ota.maximum_receive = 128;
    timeout_ota.timeout_after_bytes = 128;
    timeout_ota.timeout_advance_us = 120000ULL * 1000ULL;
    timeout_ota.headers["X-Sirokko-Token"] = "abc";
    timeout_ota.headers["X-Sirokko-State-Version"] = "22";
    timeout_ota.headers["X-Sirokko-Expires"] = "1000";
    timeout_ota.headers["X-Sirokko-Command-Id"] = "c";
    timeout_ota.headers["X-Sirokko-Upload-Size"] = std::to_string(package.size());
    timeout_ota.headers["X-Sirokko-Upload-SHA256"] = package_digest_hex;
    const int timeout_aborts_before = fake_esp::ota_abort_count;
    check(ota_handler(&timeout_ota) == ESP_OK && timeout_ota.response_status == "409 Conflict" &&
          timeout_ota.response_body.find("OTA_TIMEOUT") != std::string::npos && fake_esp::boot_partition == nullptr &&
          !fake_esp::restart_requested && fake_esp::ota_abort_count == timeout_aborts_before + 1,
          "bounded OTA deadline aborts an interrupted stream and never activates a slot");

    fake_esp::now_us += 16ULL * 60ULL * 1000ULL * 1000ULL;
    httpd_req_t expired_session = request_with_body({}, cookie);
    check(state_handler(&expired_session) == ESP_OK && expired_session.response_status == "401 Unauthorized",
          "server-side session expiry rejects an old cookie");

    if (failures == 0) {
        std::puts("PASS: actual web handlers, auth, state, grants, STOP, bounded signed OTA and abort paths");
    }
    return failures == 0 ? 0 : 1;
}
