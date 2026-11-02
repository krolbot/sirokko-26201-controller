#include "runtime_api.hpp"

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr size_t kBodyLimit = 768;
char g_password[21]{};
char g_session[33]{};
uint64_t g_last_login_ms{};
unsigned g_failed_logins{};

struct CommandResult {
    uint64_t id{};
    bool accepted{};
};
std::array<CommandResult, 16> g_results{};
size_t g_result_index{};

constexpr char kPage[] = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sirokko</title><style>
:root{color-scheme:dark;font:16px system-ui;background:#111;color:#eee}body{max-width:760px;margin:auto;padding:16px}h1{font-size:1.5rem}.card{background:#1c1c1c;border:1px solid #444;border-radius:12px;padding:14px;margin:12px 0}.warn{color:#ffca61}.bad{color:#ff6b6b}.good{color:#6bdc8b}button,input{font:inherit;padding:12px;border-radius:8px;border:1px solid #666;background:#292929;color:#fff}button{cursor:pointer}button.stop{background:#852828}button.start{background:#176b3a}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.value{font-size:1.25rem}label{display:grid;gap:5px}pre{white-space:pre-wrap;overflow-wrap:anywhere}@media(max-width:480px){button,input{width:100%;box-sizing:border-box}}
</style></head><body><h1>Sirokko 262.01</h1>
<div class="card warn"><b>DRAFT / профиль горения не квалифицирован.</b><br>Топливо программно заблокировано. Команды вентилятора и реле не являются подтверждением физической работы.</div>
<div id="login" class="card"><label>Пароль точки доступа<input id="password" type="password" autocomplete="current-password"></label><br><button onclick="login()">Войти</button><span id="loginResult"></span></div>
<div id="panel" hidden>
<div class="card"><div class="grid"><div>Состояние<div id="state" class="value">—</div></div><div>Запрет пуска<div id="block" class="value">—</div></div><div>Версия<div id="version">—</div></div></div><pre id="faults"></pre></div>
<div class="card"><div class="grid"><div>Помещение<div id="room" class="value">—</div></div><div>Выхлоп<div id="exhaust" class="value">—</div></div><div>Воздух<div id="air" class="value">—</div></div></div></div>
<div class="card grid"><button class="start" onclick="command('start')">ПУСК</button><button class="stop" onclick="command('stop')">СТОП</button><button onclick="command('reset_fault')">Сбросить аварию</button></div>
<div class="card"><div class="grid"><label>Длительность, мин<input id="duration" type="number" min="1" max="10080" value="60"></label><label>Отложить, мин<input id="delay" type="number" min="1" max="10080" value="10"></label><label>Уставка, °C<input id="setpoint" type="number" min="5" max="40" step="0.1" value="20"></label></div><br><button onclick="command('settings')">Сохранить настройки</button> <button onclick="command('arm')">Взвести отложенный пуск</button></div>
<div class="card"><label>Локальный OTA-образ<input id="ota" type="file" accept=".bin"></label><br><button onclick="uploadOta()">Загрузить только из безопасного IDLE</button></div>
<div id="last"></div><pre id="result"></pre></div>
<script>
const q=id=>document.getElementById(id);let timer;
async function login(){let r=await fetch('/api/login',{method:'POST',headers:{'X-Sirokko-Password':q('password').value}});q('loginResult').textContent=r.ok?' OK':' ОТКАЗ';if(r.ok){q('password').value='';q('login').hidden=true;q('panel').hidden=false;poll();timer=setInterval(poll,1000)}}
const temp=s=>s.valid?(s.milli_c/1000).toFixed(1)+' °C':'ОШИБКА ('+s.status+')';
async function poll(){try{let r=await fetch('/api/state',{cache:'no-store'});if(!r.ok){clearInterval(timer);q('panel').hidden=true;q('login').hidden=false;return}let s=await r.json();q('state').textContent=s.state;q('state').className='value';q('block').textContent=s.start_block;q('version').textContent=s.version;q('faults').textContent='faults=0x'+s.faults.toString(16)+'\nGLOW='+s.outputs.glow+' FUEL='+s.outputs.fuel+' FAN='+s.outputs.fan+' PWM='+s.outputs.speed_code+'\nrun_id='+s.run_id+' arm_id='+s.arm_id;q('room').textContent=temp(s.room);q('exhaust').textContent=temp(s.exhaust);q('air').textContent=temp(s.air);q('duration').value=Math.round(s.settings.duration_ms/60000);q('setpoint').value=(s.settings.setpoint_milli_c/1000).toFixed(1);q('last').textContent='Последнее подтверждение: '+new Date().toLocaleTimeString()}catch(e){q('state').textContent='СВЯЗЬ ПОТЕРЯНА — СОСТОЯНИЕ НЕИЗВЕСТНО';q('state').className='value bad';q('last').textContent='Последние значения устарели: '+new Date().toLocaleTimeString()}}
function values(action){return {action,delay_ms:Math.round(Number(q('delay').value)*60000),duration_ms:Math.round(Number(q('duration').value)*60000),setpoint_milli_c:Math.round(Number(q('setpoint').value)*1000),hysteresis_milli_c:1000}}
async function challenge(v){let p=new URLSearchParams(v);let r=await fetch('/api/challenge?'+p);if(!r.ok)throw Error(await r.text());return r.json()}
function commandId(){let a=new Uint32Array(2);crypto.getRandomValues(a);return a[0].toString(16).padStart(8,'0')+a[1].toString(16).padStart(8,'0')}
async function command(action){try{let v=values(action);if(action==='stop'){let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,command_id:commandId()})});q('result').textContent=await r.text();return}let g=await challenge(v);let body={...v,...g,command_id:commandId()};let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});q('result').textContent=await r.text();poll()}catch(e){q('result').textContent=String(e)}}
async function uploadOta(){try{let file=q('ota').files[0];if(!file)throw Error('Выберите .bin');let g=await challenge(values('ota'));let r=await fetch('/api/ota',{method:'POST',headers:{'X-Action-Token':g.token,'X-State-Version':g.state_version,'X-Expires':g.expires_ms},body:file});q('result').textContent=await r.text()}catch(e){q('result').textContent=String(e)}}
</script></body></html>)HTML";

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

bool constant_equal(const char* a, const char* b, size_t size) {
    uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) difference |= static_cast<uint8_t>(a[i] ^ b[i]);
    return difference == 0;
}

void no_store(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self' 'unsafe-inline'; connect-src 'self'");
}

bool authorized(httpd_req_t* request) {
    char cookie[160]{};
    if (!g_session[0] || httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) return false;
    const char* value = std::strstr(cookie, "sirokko_session=");
    if (!value) return false;
    value += std::strlen("sirokko_session=");
    return std::strlen(value) >= 32 && constant_equal(value, g_session, 32) &&
           (value[32] == 0 || value[32] == ';');
}

esp_err_t reject(httpd_req_t* request, const char* reason, httpd_err_code_t code = HTTPD_400_BAD_REQUEST) {
    no_store(request);
    httpd_resp_send_err(request, code, reason);
    return ESP_FAIL;
}

bool read_body(httpd_req_t* request, char* output, size_t capacity) {
    if (request->content_len <= 0 || static_cast<size_t>(request->content_len) >= capacity) return false;
    size_t offset = 0;
    while (offset < static_cast<size_t>(request->content_len)) {
        const int received = httpd_req_recv(request, output + offset, request->content_len - offset);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) return false;
        offset += received;
    }
    output[offset] = 0;
    return true;
}

bool json_u64(const cJSON* root, const char* key, uint64_t* value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 9007199254740991.0) return false;
    const uint64_t parsed = static_cast<uint64_t>(item->valuedouble);
    if (static_cast<double>(parsed) != item->valuedouble) return false;
    *value = parsed;
    return true;
}

bool json_i32(const cJSON* root, const char* key, int32_t* value) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < INT32_MIN || item->valuedouble > INT32_MAX) return false;
    const int32_t parsed = static_cast<int32_t>(item->valuedouble);
    if (static_cast<double>(parsed) != item->valuedouble) return false;
    *value = parsed;
    return true;
}

bool json_text(const cJSON* root, const char* key, char* value, size_t capacity) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring || std::strlen(item->valuestring) >= capacity) return false;
    std::strcpy(value, item->valuestring);
    return true;
}

bool parse_hex64(const char* text, uint64_t* value) {
    if (!text || !*text || std::strlen(text) > 16) return false;
    errno = 0;
    char* end{};
    const unsigned long long parsed = std::strtoull(text, &end, 16);
    if (errno || !end || *end) return false;
    *value = parsed;
    return true;
}

const char* action_name(sirokko::Action action) {
    switch (action) {
        case sirokko::Action::START: return "start";
        case sirokko::Action::ARM: return "arm";
        case sirokko::Action::RESET_FAULT: return "reset_fault";
        case sirokko::Action::SETTINGS: return "settings";
        case sirokko::Action::OTA: return "ota";
    }
    return "";
}

bool parse_action(const char* text, sirokko::Action* action) {
    if (!text || !action) return false;
    for (const auto candidate : {sirokko::Action::START, sirokko::Action::ARM,
         sirokko::Action::RESET_FAULT, sirokko::Action::SETTINGS, sirokko::Action::OTA}) {
        if (std::strcmp(text, action_name(candidate)) == 0) { *action = candidate; return true; }
    }
    return false;
}

uint64_t hash_parameters(sirokko::Action action, uint64_t delay, const sirokko::Settings& settings) {
    uint64_t hash = 1469598103934665603ULL;
    const uint64_t values[]{static_cast<uint64_t>(action), delay, settings.duration_ms,
                            static_cast<uint32_t>(settings.room_setpoint_milli_c),
                            static_cast<uint32_t>(settings.room_hysteresis_milli_c)};
    for (uint64_t value : values) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash = (hash ^ static_cast<uint8_t>(value >> (byte * 8))) * 1099511628211ULL;
        }
    }
    return hash;
}

bool query_u64(const char* query, const char* key, uint64_t* value) {
    char text[24]{};
    if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK) return false;
    errno = 0;
    char* end{};
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno || !end || *end) return false;
    *value = parsed;
    return true;
}

bool query_i32(const char* query, const char* key, int32_t* value) {
    uint64_t parsed{};
    if (!query_u64(query, key, &parsed) || parsed > INT32_MAX) return false;
    *value = static_cast<int32_t>(parsed);
    return true;
}

esp_err_t page_handler(httpd_req_t* request) {
    no_store(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t login_handler(httpd_req_t* request) {
    no_store(request);
    const uint64_t now = now_ms();
    if ((g_failed_logins >= 5 && now - g_last_login_ms < 30000) || now - g_last_login_ms < 500) {
        return reject(request, "RATE_LIMIT", HTTPD_400_BAD_REQUEST);
    }
    g_last_login_ms = now;
    char supplied[32]{};
    const bool header = httpd_req_get_hdr_value_str(request, "X-Sirokko-Password", supplied, sizeof(supplied)) == ESP_OK;
    const bool ok = header && std::strlen(supplied) == 20 && constant_equal(supplied, g_password, 20);
    std::memset(supplied, 0, sizeof(supplied));
    if (!ok) {
        ++g_failed_logins;
        return reject(request, "AUTH", HTTPD_401_UNAUTHORIZED);
    }
    g_failed_logins = 0;
    if (!g_session[0]) {
        for (size_t i = 0; i < 16; ++i) std::snprintf(g_session + i * 2, 3, "%02x", esp_random() & 0xFFU);
    }
    char cookie[96]{};
    std::snprintf(cookie, sizeof(cookie), "sirokko_session=%s; HttpOnly; SameSite=Strict; Path=/", g_session);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"status\":\"OK\"}");
}

esp_err_t state_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", HTTPD_401_UNAUTHORIZED);
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", HTTPD_500_INTERNAL_SERVER_ERROR);
    char json[2048]{};
    const auto sensor = [](char* out, size_t cap, const sirokko::SensorSample& s) {
        std::snprintf(out, cap, "{\"milli_c\":%ld,\"sample_ms\":%llu,\"status\":%u,\"valid\":%s}",
                      static_cast<long>(s.milli_c), static_cast<unsigned long long>(s.sample_ms), s.status,
                      s.valid ? "true" : "false");
    };
    char exhaust[160]{}, air[160]{}, room[160]{};
    sensor(exhaust, sizeof(exhaust), view.sensors.exhaust);
    sensor(air, sizeof(air), view.sensors.air);
    sensor(room, sizeof(room), view.sensors.room);
    const int size = std::snprintf(json, sizeof(json),
        "{\"version\":\"%s\",\"uptime_ms\":%llu,\"state\":\"%s\",\"start_block\":\"%s\","
        "\"faults\":%lu,\"state_version\":\"%llx\",\"run_id\":\"%llx\",\"arm_id\":\"%llx\","
        "\"profile_qualified\":%s,\"duration_active\":%s,\"duration_remaining_ms\":%llu,"
        "\"delayed_armed\":%s,\"delayed_remaining_ms\":%llu,"
        "\"outputs\":{\"fan\":%s,\"speed_code\":%u,\"glow\":%s,\"fuel\":%s},"
        "\"settings\":{\"duration_ms\":%llu,\"setpoint_milli_c\":%ld,\"hysteresis_milli_c\":%ld},"
        "\"exhaust\":%s,\"air\":%s,\"room\":%s}",
        esp_app_get_description()->version, static_cast<unsigned long long>(view.now_ms),
        sirokko::state_name(view.control.state), sirokko::block_name(view.control.start_block),
        static_cast<unsigned long>(view.control.faults), static_cast<unsigned long long>(view.control.state_version),
        static_cast<unsigned long long>(view.control.run_id), static_cast<unsigned long long>(view.control.arm_id),
        view.control.profile_qualified ? "true" : "false", view.control.duration_active ? "true" : "false",
        static_cast<unsigned long long>(view.control.duration_remaining_ms), view.control.delayed_armed ? "true" : "false",
        static_cast<unsigned long long>(view.control.delayed_remaining_ms), view.control.outputs.fan_request ? "true" : "false",
        view.control.outputs.speed_code, view.control.outputs.glow ? "true" : "false", view.control.outputs.fuel ? "true" : "false",
        static_cast<unsigned long long>(view.settings.duration_ms), static_cast<long>(view.settings.room_setpoint_milli_c),
        static_cast<long>(view.settings.room_hysteresis_milli_c), exhaust, air, room);
    if (size <= 0 || static_cast<size_t>(size) >= sizeof(json)) return reject(request, "ENCODING", HTTPD_500_INTERNAL_SERVER_ERROR);
    no_store(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json, size);
}

esp_err_t challenge_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", HTTPD_401_UNAUTHORIZED);
    char query[256]{}, action_text[24]{};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "action", action_text, sizeof(action_text)) != ESP_OK)
        return reject(request, "QUERY");
    sirokko::Action action{};
    if (!parse_action(action_text, &action)) return reject(request, "ACTION");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY");
    uint64_t delay = 0;
    sirokko::Settings settings = view.settings;
    if (action == sirokko::Action::ARM && !query_u64(query, "delay_ms", &delay)) return reject(request, "DELAY");
    if (action == sirokko::Action::SETTINGS &&
        (!query_u64(query, "duration_ms", &settings.duration_ms) ||
         !query_i32(query, "setpoint_milli_c", &settings.room_setpoint_milli_c) ||
         !query_i32(query, "hysteresis_milli_c", &settings.room_hysteresis_milli_c))) return reject(request, "SETTINGS");
    sirokko::Grant grant{};
    if (!runtime_issue(action, hash_parameters(action, delay, settings), &grant)) return reject(request, "BUSY");
    char response[192]{};
    std::snprintf(response, sizeof(response),
                  "{\"token\":\"%llx\",\"state_version\":\"%llx\",\"expires_ms\":\"%llx\"}",
                  static_cast<unsigned long long>(grant.token), static_cast<unsigned long long>(grant.state_version),
                  static_cast<unsigned long long>(grant.expires_ms));
    no_store(request);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, response);
}

bool prior_result(uint64_t id, bool* accepted) {
    for (const auto& result : g_results) if (result.id == id && id != 0) { *accepted = result.accepted; return true; }
    return false;
}

void remember_result(uint64_t id, bool accepted) {
    g_results[g_result_index++ % g_results.size()] = {id, accepted};
}

esp_err_t command_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", HTTPD_401_UNAUTHORIZED);
    char body[kBodyLimit]{};
    if (!read_body(request, body, sizeof(body))) return reject(request, "BODY");
    cJSON* root = cJSON_Parse(body);
    std::memset(body, 0, sizeof(body));
    if (!root || !cJSON_IsObject(root)) { if (root) cJSON_Delete(root); return reject(request, "JSON"); }
    char action_text[24]{}, command_id_text[24]{};
    if (!json_text(root, "action", action_text, sizeof(action_text)) ||
        !json_text(root, "command_id", command_id_text, sizeof(command_id_text))) {
        cJSON_Delete(root); return reject(request, "FIELDS");
    }
    uint64_t command_id{};
    if (!parse_hex64(command_id_text, &command_id) || !command_id) { cJSON_Delete(root); return reject(request, "COMMAND_ID"); }
    bool previous{};
    if (prior_result(command_id, &previous)) {
        cJSON_Delete(root);
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, previous ? "{\"status\":\"QUEUED\",\"replay\":true}" : "{\"status\":\"REJECTED\",\"replay\":true}");
    }
    if (std::strcmp(action_text, "stop") == 0) {
        runtime_stop();
        remember_result(command_id, true);
        cJSON_Delete(root);
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "{\"status\":\"STOP_REQUESTED\"}");
    }

    sirokko::Action action{};
    char token_text[24]{}, version_text[24]{}, expires_text[24]{};
    if (!parse_action(action_text, &action) || action == sirokko::Action::OTA ||
        !json_text(root, "token", token_text, sizeof(token_text)) ||
        !json_text(root, "state_version", version_text, sizeof(version_text)) ||
        !json_text(root, "expires_ms", expires_text, sizeof(expires_text))) {
        cJSON_Delete(root); return reject(request, "GRANT");
    }
    RuntimeView view{};
    runtime_view(&view);
    uint64_t delay = 0;
    sirokko::Settings settings = view.settings;
    if (action == sirokko::Action::ARM && !json_u64(root, "delay_ms", &delay)) { cJSON_Delete(root); return reject(request, "DELAY"); }
    if (action == sirokko::Action::SETTINGS &&
        (!json_u64(root, "duration_ms", &settings.duration_ms) ||
         !json_i32(root, "setpoint_milli_c", &settings.room_setpoint_milli_c) ||
         !json_i32(root, "hysteresis_milli_c", &settings.room_hysteresis_milli_c))) {
        cJSON_Delete(root); return reject(request, "SETTINGS");
    }
    sirokko::Grant grant{};
    grant.action = action;
    grant.params_hash = hash_parameters(action, delay, settings);
    grant.valid = parse_hex64(token_text, &grant.token) && parse_hex64(version_text, &grant.state_version) &&
                  parse_hex64(expires_text, &grant.expires_ms);
    const bool accepted = grant.valid && runtime_submit(grant, action, delay,
        action == sirokko::Action::SETTINGS ? &settings : nullptr);
    remember_result(command_id, accepted);
    cJSON_Delete(root);
    no_store(request);
    httpd_resp_set_type(request, "application/json");
    if (!accepted) { httpd_resp_set_status(request, "409 Conflict"); return httpd_resp_sendstr(request, "{\"status\":\"REJECTED\"}"); }
    return httpd_resp_sendstr(request, "{\"status\":\"QUEUED\"}");
}

esp_err_t ota_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", HTTPD_401_UNAUTHORIZED);
    char token_text[24]{}, version_text[24]{}, expires_text[24]{};
    if (httpd_req_get_hdr_value_str(request, "X-Action-Token", token_text, sizeof(token_text)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "X-State-Version", version_text, sizeof(version_text)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "X-Expires", expires_text, sizeof(expires_text)) != ESP_OK)
        return reject(request, "GRANT");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY");
    sirokko::Grant grant{};
    grant.action = sirokko::Action::OTA;
    grant.params_hash = hash_parameters(grant.action, 0, view.settings);
    grant.valid = parse_hex64(token_text, &grant.token) && parse_hex64(version_text, &grant.state_version) &&
                  parse_hex64(expires_text, &grant.expires_ms);
    if (!grant.valid || !runtime_submit(grant, grant.action, 0, nullptr)) return reject(request, "NOT_SAFE");
    const uint64_t wait_started = now_ms();
    while (!runtime_maintenance_safe() && now_ms() - wait_started < 1000) vTaskDelay(pdMS_TO_TICKS(10));
    if (!runtime_maintenance_safe()) { runtime_exit_maintenance(); return reject(request, "NOT_SAFE"); }

    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || request->content_len < 1024 || request->content_len > static_cast<int>(partition->size)) {
        runtime_exit_maintenance(); return reject(request, "SIZE");
    }
    esp_ota_handle_t handle{};
    if (esp_ota_begin(partition, request->content_len, &handle) != ESP_OK) {
        runtime_exit_maintenance(); return reject(request, "OTA_BEGIN", HTTPD_500_INTERNAL_SERVER_ERROR);
    }
    std::array<char, 4096> buffer{};
    int remaining = request->content_len;
    bool ok = true;
    while (ok && remaining > 0) {
        const int received = httpd_req_recv(request, buffer.data(), std::min<int>(remaining, buffer.size()));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0 || !runtime_maintenance_safe()) { ok = false; break; }
        ok = esp_ota_write(handle, buffer.data(), received) == ESP_OK;
        remaining -= received;
    }
    std::fill(buffer.begin(), buffer.end(), 0);
    if (!ok || remaining != 0 || esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) {
        esp_ota_abort(handle);
        runtime_exit_maintenance();
        return reject(request, "OTA_VERIFY", HTTPD_500_INTERNAL_SERVER_ERROR);
    }
    no_store(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"status\":\"VERIFIED_REBOOTING\"}");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

}  // namespace

bool web_start(const char* ap_password) {
    if (!ap_password || std::strlen(ap_password) != 20) return false;
    std::memcpy(g_password, ap_password, 21);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server{};
    if (httpd_start(&server, &config) != ESP_OK) return false;
    const httpd_uri_t handlers[] = {
        {"/", HTTP_GET, page_handler, nullptr},
        {"/api/login", HTTP_POST, login_handler, nullptr},
        {"/api/state", HTTP_GET, state_handler, nullptr},
        {"/api/challenge", HTTP_GET, challenge_handler, nullptr},
        {"/api/command", HTTP_POST, command_handler, nullptr},
        {"/api/ota", HTTP_POST, ota_handler, nullptr},
    };
    for (const auto& handler : handlers) if (httpd_register_uri_handler(server, &handler) != ESP_OK) return false;
    return true;
}
