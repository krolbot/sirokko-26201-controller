#include "runtime_api.hpp"
#include "bounded_receive.hpp"
#include "ota_verify.hpp"
#include "web_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"

namespace web_protocol = sirokko::web_protocol;
namespace ota_verify = sirokko::ota_verify;

namespace {

constexpr size_t kCommandBodyLimit = 4096;
constexpr size_t kStateJsonCapacity = 8192;
constexpr uint64_t kCommandBodyReadBudgetMs = 3000;
constexpr uint64_t kOtaBodyReadBudgetMs = 120000;
constexpr uint64_t kMaintenanceWaitMs = 1000;
constexpr uint64_t kSessionLifetimeMs = 15ULL * 60ULL * 1000ULL;
constexpr char kProjectName[] = "sirokko_26201_controller";
constexpr char kTargetName[] = "esp32s3";

extern const uint8_t ota_public_key_pem_start[] asm("_binary_ota_public_key_pem_start");
extern const uint8_t ota_public_key_pem_end[] asm("_binary_ota_public_key_pem_end");

char g_password[21]{};
char g_session[33]{};
uint64_t g_session_expires_ms{};
uint64_t g_last_login_ms{};
unsigned g_failed_logins{};
web_protocol::ChallengeBinding g_challenge{};
web_protocol::ReplayCache g_replays{};
std::array<char, kStateJsonCapacity> g_state_json{};

constexpr char kPage[] = R"SIROKKO(<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sirokko 262.01</title><style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;background:#101216;color:#edf0f4}body{max-width:1050px;margin:auto;padding:14px}h1{font-size:1.55rem;margin:.3rem 0}h2{font-size:1.12rem;margin:.2rem 0 .7rem}.card{background:#1b2028;border:1px solid #3b4657;border-radius:12px;padding:14px;margin:12px 0}.notice{border-left:5px solid #e7b44f}.warn{color:#ffd273}.bad{color:#ff8d8d}.good{color:#8be3a8}.stale{background:#5a2525;border-color:#ff8d8d}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}.value{font-size:1.12rem;overflow-wrap:anywhere}.small{font-size:.88rem;color:#c4cad4}label{display:grid;gap:5px}button,input,select,textarea{font:inherit;padding:10px;border-radius:7px;border:1px solid #637084;background:#11161e;color:#fff}button{cursor:pointer}button:disabled{opacity:.45;cursor:not-allowed}.start{background:#17693c}.stop{background:#8c2e31;font-weight:700}.secondary{background:#243447}textarea{min-height:190px;resize:vertical}table{width:100%;border-collapse:collapse;font-size:.84rem}th,td{text-align:left;vertical-align:top;border-bottom:1px solid #394353;padding:6px;overflow-wrap:anywhere}pre{white-space:pre-wrap;overflow-wrap:anywhere;margin:0}.row{display:flex;gap:8px;align-items:end;flex-wrap:wrap}.row>*{flex:1}.hidden{display:none!important}@media(max-width:520px){body{padding:8px}.row>*{min-width:100%}button,input,select,textarea{box-sizing:border-box;width:100%}}
</style></head><body>
<h1>Sirokko 262.01</h1>
<div class="card notice"><b>Сервисный интерфейс, не подтверждение физической работы.</b><br><span class="small">Профиль процесса валидируется как форма данных; <code>physical_tests=false</code>. Команда вентилятора — не измеренные RPM, ток, расход или состояние реле.</span></div>
<div id="login" class="card"><h2>Вход в точку доступа</h2><label>Пароль AP<input id="password" type="password" autocomplete="current-password"></label><p><button id="loginButton" onclick="login()">Войти</button> <span id="loginResult"></span></p></div>
<div id="panel" hidden>
<div id="stale" class="card stale hidden"><b>СВЯЗЬ ПОТЕРЯНА — данные устарели.</b><br>ПУСК, настройки, таймер, калибровка, профиль и OTA заблокированы до нового чтения состояния. СТОП остаётся доступным отдельной приоритетной командой.</div>
<div class="card"><div class="grid"><div>Состояние<div id="state" class="value">—</div></div><div>Запрет пуска<div id="block" class="value">—</div></div><div>Версия<div id="version" class="value">—</div></div><div>Хранилище<div id="storage" class="value">—</div></div></div><pre id="faults" class="small"></pre><div id="last" class="small"></div></div>
<div class="card"><h2>Температуры и команда</h2><div class="grid"><div>Помещение<div id="room" class="value">—</div></div><div>Выхлоп EXH<div id="exhaust" class="value">—</div></div><div>Воздух AIR<div id="air" class="value">—</div></div><div>Команда вентилятора<div id="command" class="value">—</div></div><div>Диапазон<div id="range" class="value">—</div></div><div>Превышение комнаты<div id="roomExcess" class="value">—</div></div></div></div>
<div class="card"><h2>Сеанс, пуск и продувка</h2><div class="grid"><div>Таймер останова<div id="runRemaining" class="value">—</div></div><div>Отложенный пуск<div id="armRemaining" class="value">—</div></div><div>Продувка<div id="purgeRemaining" class="value">—</div></div><div>Калибровка<div id="calibration" class="value">—</div></div></div><div id="purgeConditions" class="small"></div></div>
<div class="card"><h2>Управление</h2><div class="grid"><button class="start" data-online onclick="issueSimple('START')">ПУСК</button><button class="stop" id="stopButton" onclick="stopNow()">СТОП</button><button data-online onclick="issueSimple('RESET_FAULT')">Сбросить аварию</button><button data-online onclick="issueSimple('CALIBRATE')">Начать автоматическую калибровку</button><button data-online onclick="cancelArm()">Отменить отложенный пуск</button></div><p class="small">СТОП не требует дополнительного подтверждения и никогда не обращается к GPIO напрямую: он передаётся в общий автомат.</p></div>
<div class="card"><h2>Настройки управления</h2><div class="grid"><label>Уставка помещения, °C<input id="setpoint" data-setting type="number" min="15" max="30" step="1"></label><label>Длительность нового сеанса, мин (0=выкл.)<input id="duration" data-setting type="number" min="0" max="10080" step="1"></label><label>Режим<select id="mode" data-setting><option value="TEMPERATURE">По температуре</option><option value="MANUAL">Ручной</option></select></label><label>Ручная команда, %<input id="manual" data-setting type="number" min="5" max="100" step="5"></label><label>Нарастание /с<input id="slewUp" data-setting type="number" min="0.0001" max="0.005" step="0.0001"></label><label>Снижение /с<input id="slewDown" data-setting type="number" min="0.0001" max="0.005" step="0.0001"></label></div><p><button class="secondary" data-online onclick="saveSettings()">Сохранить настройки</button></p><p class="small">Старый гистерезис не является полем UI. Редактирование не перезапускает уже работающий таймер.</p></div>
<div class="card"><h2>Таймер и отложенный пуск</h2><div class="row"><label>Выключить через, мин (0=отмена)<input id="timerAfter" type="number" min="0" max="10080" step="1" value="0"></label><button class="secondary" data-online onclick="setTimer()">Установить таймер от сейчас</button><label>Пуск через, мин<input id="armAfter" type="number" min="1" max="10080" step="1" value="10"></label><button class="secondary" data-online onclick="armStart()">Взвести пуск</button></div></div>
<div class="card"><h2>Профиль процесса — сервисный JSON</h2><p class="small">Вставьте полный JSON профиля из утверждённого паспорта процесса. Форма проверяется строго; здесь намеренно нет вымышленных физических чисел или поэтапных подтверждений. <code>qualified</code> означает настроенную форму профиля, а не доказанную квалификацию горения.</p><textarea id="processJson" spellcheck="false" placeholder="Вставьте полный JSON профиля процесса без примеров чисел."></textarea><p><button class="secondary" data-online onclick="saveProcess()">Проверить и передать профиль</button></p><div id="profileStatus" class="small">—</div></div>
<div class="card"><h2>Подписанное OTA</h2><p class="small">Принимается один подписанный пакет <code>.sota</code>, созданный offline-инструментом. Обычный совместимый <code>.bin</code> не является подлинным OTA-пакетом и не принимается. Загрузка доступна только после безопасного перехода в обслуживание.</p><div class="row"><label>Файл .sota<input id="otaFile" type="file" accept=".sota,application/octet-stream"></label><button class="secondary" data-online onclick="uploadOta()">Проверить и загрузить OTA</button></div></div>
<div class="card"><h2>Журнал автомата (монотонное время)</h2><div class="small">В том числе отрицательное сообщение KEY0 во время калибровки, если оно зафиксировано автоматом.</div><div style="overflow:auto"><table><thead><tr><th>ms</th><th>Событие</th><th>Состояние</th><th>PWM</th><th>EXH / AIR</th><th>Faults</th></tr></thead><tbody id="events"></tbody></table></div></div>
<div class="card"><b>Ответ интерфейса</b><pre id="result">—</pre></div>
</div>
<script>
/* BEGIN js-sha256 0.11.1 (MIT) */
/**
 * [js-sha256]{@link https://github.com/emn178/js-sha256}
 *
 * @version 0.11.1
 * @author Chen, Yi-Cyuan [emn178@gmail.com]
 * @copyright Chen, Yi-Cyuan 2014-2025
 * @license MIT
 */
!function(){"use strict";function t(t,i){i?(d[0]=d[16]=d[1]=d[2]=d[3]=d[4]=d[5]=d[6]=d[7]=d[8]=d[9]=d[10]=d[11]=d[12]=d[13]=d[14]=d[15]=0,this.blocks=d):this.blocks=[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],t?(this.h0=3238371032,this.h1=914150663,this.h2=812702999,this.h3=4144912697,this.h4=4290775857,this.h5=1750603025,this.h6=1694076839,this.h7=3204075428):(this.h0=1779033703,this.h1=3144134277,this.h2=1013904242,this.h3=2773480762,this.h4=1359893119,this.h5=2600822924,this.h6=528734635,this.h7=1541459225),this.block=this.start=this.bytes=this.hBytes=0,this.finalized=this.hashed=!1,this.first=!0,this.is224=t}function i(i,r,s){var e,n=typeof i;if("string"===n){var o,a=[],u=i.length,c=0;for(e=0;e<u;++e)(o=i.charCodeAt(e))<128?a[c++]=o:o<2048?(a[c++]=192|o>>>6,a[c++]=128|63&o):o<55296||o>=57344?(a[c++]=224|o>>>12,a[c++]=128|o>>>6&63,a[c++]=128|63&o):(o=65536+((1023&o)<<10|1023&i.charCodeAt(++e)),a[c++]=240|o>>>18,a[c++]=128|o>>>12&63,a[c++]=128|o>>>6&63,a[c++]=128|63&o);i=a}else{if("object"!==n)throw new Error(h);if(null===i)throw new Error(h);if(f&&i.constructor===ArrayBuffer)i=new Uint8Array(i);else if(!(Array.isArray(i)||f&&ArrayBuffer.isView(i)))throw new Error(h)}i.length>64&&(i=new t(r,!0).update(i).array());var y=[],p=[];for(e=0;e<64;++e){var l=i[e]||0;y[e]=92^l,p[e]=54^l}t.call(this,r,s),this.update(p),this.oKeyPad=y,this.inner=!0,this.sharedMemory=s}var h="input is invalid type",r="object"==typeof window,s=r?window:{};s.JS_SHA256_NO_WINDOW&&(r=!1);var e=!r&&"object"==typeof self,n=!s.JS_SHA256_NO_NODE_JS&&"object"==typeof process&&process.versions&&process.versions.node&&"renderer"!=process.type;n?s=global:e&&(s=self);var o=!s.JS_SHA256_NO_COMMON_JS&&"object"==typeof module&&module.exports,a="function"==typeof define&&define.amd,f=!s.JS_SHA256_NO_ARRAY_BUFFER&&"undefined"!=typeof ArrayBuffer,u="0123456789abcdef".split(""),c=[-2147483648,8388608,32768,128],y=[24,16,8,0],p=[1116352408,1899447441,3049323471,3921009573,961987163,1508970993,2453635748,2870763221,3624381080,310598401,607225278,1426881987,1925078388,2162078206,2614888103,3248222580,3835390401,4022224774,264347078,604807628,770255983,1249150122,1555081692,1996064986,2554220882,2821834349,2952996808,3210313671,3336571891,3584528711,113926993,338241895,666307205,773529912,1294757372,1396182291,1695183700,1986661051,2177026350,2456956037,2730485921,2820302411,3259730800,3345764771,3516065817,3600352804,4094571909,275423344,430227734,506948616,659060556,883997877,958139571,1322822218,1537002063,1747873779,1955562222,2024104815,2227730452,2361852424,2428436474,2756734187,3204031479,3329325298],l=["hex","array","digest","arrayBuffer"],d=[];!s.JS_SHA256_NO_NODE_JS&&Array.isArray||(Array.isArray=function(t){return"[object Array]"===Object.prototype.toString.call(t)}),!f||!s.JS_SHA256_NO_ARRAY_BUFFER_IS_VIEW&&ArrayBuffer.isView||(ArrayBuffer.isView=function(t){return"object"==typeof t&&t.buffer&&t.buffer.constructor===ArrayBuffer});var A=function(i,h){return function(r){return new t(h,!0).update(r)[i]()}},w=function(i){var h=A("hex",i);n&&(h=b(h,i)),h.create=function(){return new t(i)},h.update=function(t){return h.create().update(t)};for(var r=0;r<l.length;++r){var s=l[r];h[s]=A(s,i)}return h},b=function(t,i){var r,e=require("crypto"),n=require("buffer").Buffer,o=i?"sha224":"sha256";r=n.from&&!s.JS_SHA256_NO_BUFFER_FROM?n.from:function(t){return new n(t)};return function(i){if("string"==typeof i)return e.createHash(o).update(i,"utf8").digest("hex");if(null===i||void 0===i)throw new Error(h);return i.constructor===ArrayBuffer&&(i=new Uint8Array(i)),Array.isArray(i)||ArrayBuffer.isView(i)||i.constructor===n?e.createHash(o).update(r(i)).digest("hex"):t(i)}},_=function(t,h){return function(r,s){return new i(r,h,!0).update(s)[t]()}},v=function(t){var h=_("hex",t);h.create=function(h){return new i(h,t)},h.update=function(t,i){return h.create(t).update(i)};for(var r=0;r<l.length;++r){var s=l[r];h[s]=_(s,t)}return h};t.prototype.update=function(t){if(!this.finalized){var i,r=typeof t;if("string"!==r){if("object"!==r)throw new Error(h);if(null===t)throw new Error(h);if(f&&t.constructor===ArrayBuffer)t=new Uint8Array(t);else if(!(Array.isArray(t)||f&&ArrayBuffer.isView(t)))throw new Error(h);i=!0}for(var s,e,n=0,o=t.length,a=this.blocks;n<o;){if(this.hashed&&(this.hashed=!1,a[0]=this.block,this.block=a[16]=a[1]=a[2]=a[3]=a[4]=a[5]=a[6]=a[7]=a[8]=a[9]=a[10]=a[11]=a[12]=a[13]=a[14]=a[15]=0),i)for(e=this.start;n<o&&e<64;++n)a[e>>>2]|=t[n]<<y[3&e++];else for(e=this.start;n<o&&e<64;++n)(s=t.charCodeAt(n))<128?a[e>>>2]|=s<<y[3&e++]:s<2048?(a[e>>>2]|=(192|s>>>6)<<y[3&e++],a[e>>>2]|=(128|63&s)<<y[3&e++]):s<55296||s>=57344?(a[e>>>2]|=(224|s>>>12)<<y[3&e++],a[e>>>2]|=(128|s>>>6&63)<<y[3&e++],a[e>>>2]|=(128|63&s)<<y[3&e++]):(s=65536+((1023&s)<<10|1023&t.charCodeAt(++n)),a[e>>>2]|=(240|s>>>18)<<y[3&e++],a[e>>>2]|=(128|s>>>12&63)<<y[3&e++],a[e>>>2]|=(128|s>>>6&63)<<y[3&e++],a[e>>>2]|=(128|63&s)<<y[3&e++]);this.lastByteIndex=e,this.bytes+=e-this.start,e>=64?(this.block=a[16],this.start=e-64,this.hash(),this.hashed=!0):this.start=e}return this.bytes>4294967295&&(this.hBytes+=this.bytes/4294967296<<0,this.bytes=this.bytes%4294967296),this}},t.prototype.finalize=function(){if(!this.finalized){this.finalized=!0;var t=this.blocks,i=this.lastByteIndex;t[16]=this.block,t[i>>>2]|=c[3&i],this.block=t[16],i>=56&&(this.hashed||this.hash(),t[0]=this.block,t[16]=t[1]=t[2]=t[3]=t[4]=t[5]=t[6]=t[7]=t[8]=t[9]=t[10]=t[11]=t[12]=t[13]=t[14]=t[15]=0),t[14]=this.hBytes<<3|this.bytes>>>29,t[15]=this.bytes<<3,this.hash()}},t.prototype.hash=function(){var t,i,h,r,s,e,n,o,a,f=this.h0,u=this.h1,c=this.h2,y=this.h3,l=this.h4,d=this.h5,A=this.h6,w=this.h7,b=this.blocks;for(t=16;t<64;++t)i=((s=b[t-15])>>>7|s<<25)^(s>>>18|s<<14)^s>>>3,h=((s=b[t-2])>>>17|s<<15)^(s>>>19|s<<13)^s>>>10,b[t]=b[t-16]+i+b[t-7]+h<<0;for(a=u&c,t=0;t<64;t+=4)this.first?(this.is224?(e=300032,w=(s=b[0]-1413257819)-150054599<<0,y=s+24177077<<0):(e=704751109,w=(s=b[0]-210244248)-1521486534<<0,y=s+143694565<<0),this.first=!1):(i=(f>>>2|f<<30)^(f>>>13|f<<19)^(f>>>22|f<<10),r=(e=f&u)^f&c^a,w=y+(s=w+(h=(l>>>6|l<<26)^(l>>>11|l<<21)^(l>>>25|l<<7))+(l&d^~l&A)+p[t]+b[t])<<0,y=s+(i+r)<<0),i=(y>>>2|y<<30)^(y>>>13|y<<19)^(y>>>22|y<<10),r=(n=y&f)^y&u^e,A=c+(s=A+(h=(w>>>6|w<<26)^(w>>>11|w<<21)^(w>>>25|w<<7))+(w&l^~w&d)+p[t+1]+b[t+1])<<0,i=((c=s+(i+r)<<0)>>>2|c<<30)^(c>>>13|c<<19)^(c>>>22|c<<10),r=(o=c&y)^c&f^n,d=u+(s=d+(h=(A>>>6|A<<26)^(A>>>11|A<<21)^(A>>>25|A<<7))+(A&w^~A&l)+p[t+2]+b[t+2])<<0,i=((u=s+(i+r)<<0)>>>2|u<<30)^(u>>>13|u<<19)^(u>>>22|u<<10),r=(a=u&c)^u&y^o,l=f+(s=l+(h=(d>>>6|d<<26)^(d>>>11|d<<21)^(d>>>25|d<<7))+(d&A^~d&w)+p[t+3]+b[t+3])<<0,f=s+(i+r)<<0,this.chromeBugWorkAround=!0;this.h0=this.h0+f<<0,this.h1=this.h1+u<<0,this.h2=this.h2+c<<0,this.h3=this.h3+y<<0,this.h4=this.h4+l<<0,this.h5=this.h5+d<<0,this.h6=this.h6+A<<0,this.h7=this.h7+w<<0},t.prototype.hex=function(){this.finalize();var t=this.h0,i=this.h1,h=this.h2,r=this.h3,s=this.h4,e=this.h5,n=this.h6,o=this.h7,a=u[t>>>28&15]+u[t>>>24&15]+u[t>>>20&15]+u[t>>>16&15]+u[t>>>12&15]+u[t>>>8&15]+u[t>>>4&15]+u[15&t]+u[i>>>28&15]+u[i>>>24&15]+u[i>>>20&15]+u[i>>>16&15]+u[i>>>12&15]+u[i>>>8&15]+u[i>>>4&15]+u[15&i]+u[h>>>28&15]+u[h>>>24&15]+u[h>>>20&15]+u[h>>>16&15]+u[h>>>12&15]+u[h>>>8&15]+u[h>>>4&15]+u[15&h]+u[r>>>28&15]+u[r>>>24&15]+u[r>>>20&15]+u[r>>>16&15]+u[r>>>12&15]+u[r>>>8&15]+u[r>>>4&15]+u[15&r]+u[s>>>28&15]+u[s>>>24&15]+u[s>>>20&15]+u[s>>>16&15]+u[s>>>12&15]+u[s>>>8&15]+u[s>>>4&15]+u[15&s]+u[e>>>28&15]+u[e>>>24&15]+u[e>>>20&15]+u[e>>>16&15]+u[e>>>12&15]+u[e>>>8&15]+u[e>>>4&15]+u[15&e]+u[n>>>28&15]+u[n>>>24&15]+u[n>>>20&15]+u[n>>>16&15]+u[n>>>12&15]+u[n>>>8&15]+u[n>>>4&15]+u[15&n];return this.is224||(a+=u[o>>>28&15]+u[o>>>24&15]+u[o>>>20&15]+u[o>>>16&15]+u[o>>>12&15]+u[o>>>8&15]+u[o>>>4&15]+u[15&o]),a},t.prototype.toString=t.prototype.hex,t.prototype.digest=function(){this.finalize();var t=this.h0,i=this.h1,h=this.h2,r=this.h3,s=this.h4,e=this.h5,n=this.h6,o=this.h7,a=[t>>>24&255,t>>>16&255,t>>>8&255,255&t,i>>>24&255,i>>>16&255,i>>>8&255,255&i,h>>>24&255,h>>>16&255,h>>>8&255,255&h,r>>>24&255,r>>>16&255,r>>>8&255,255&r,s>>>24&255,s>>>16&255,s>>>8&255,255&s,e>>>24&255,e>>>16&255,e>>>8&255,255&e,n>>>24&255,n>>>16&255,n>>>8&255,255&n];return this.is224||a.push(o>>>24&255,o>>>16&255,o>>>8&255,255&o),a},t.prototype.array=t.prototype.digest,t.prototype.arrayBuffer=function(){this.finalize();var t=new ArrayBuffer(this.is224?28:32),i=new DataView(t);return i.setUint32(0,this.h0),i.setUint32(4,this.h1),i.setUint32(8,this.h2),i.setUint32(12,this.h3),i.setUint32(16,this.h4),i.setUint32(20,this.h5),i.setUint32(24,this.h6),this.is224||i.setUint32(28,this.h7),t},(i.prototype=new t).finalize=function(){if(t.prototype.finalize.call(this),this.inner){this.inner=!1;var i=this.array();t.call(this,this.is224,this.sharedMemory),this.update(this.oKeyPad),this.update(i),t.prototype.finalize.call(this)}};var B=w();B.sha256=B,B.sha224=w(!0),B.sha256.hmac=v(),B.sha224.hmac=v(!0),o?module.exports=B:(s.sha256=B.sha256,s.sha224=B.sha224,a&&define(function(){return B}))}();
/* END js-sha256 */
'use strict';
const q=id=>document.getElementById(id);const dirty=new Set();let pollTimer=null;let online=false;let pending=null;let lastState=null;
for(const input of document.querySelectorAll('[data-setting]'))input.addEventListener('input',()=>dirty.add(input.id));
function text(id,value){q(id).textContent=value==null?'—':String(value)}
function milliseconds(ms){if(!Number.isFinite(ms))return'—';const seconds=Math.ceil(ms/1000);const d=Math.floor(seconds/86400),h=Math.floor(seconds%86400/3600),m=Math.floor(seconds%3600/60),s=seconds%60;return(d?d+' д ':'')+(h?h+' ч ':'')+(m?m+' мин ':'')+s+' с'}
function temperature(sample){return sample&&sample.valid?(sample.milli_c/1000).toFixed(1)+' °C':'ОШИБКА ('+(sample?sample.status:'—')+')'}
function commandId(){if(!crypto||!crypto.getRandomValues)throw Error('Браузер не предоставляет криптографический генератор');const value=new Uint32Array(2);crypto.getRandomValues(value);return value[0].toString(16).padStart(8,'0')+value[1].toString(16).padStart(8,'0')}
function report(value){text('result',value)}
function setOnline(value){online=value;q('stale').classList.toggle('hidden',value);for(const button of document.querySelectorAll('[data-online]'))button.disabled=!value;q('stopButton').disabled=false}
async function responseJson(response){const raw=await response.text();try{return{response,body:JSON.parse(raw)}}catch(_){return{response,body:{status:'HTTP_ERROR',code:raw||response.statusText}}}}
async function api(url,options){const reply=await fetch(url,options);return responseJson(reply)}
function settingsPayload(){const setpoint=Number(q('setpoint').value),duration=Number(q('duration').value),manual=Number(q('manual').value),up=Number(q('slewUp').value),down=Number(q('slewDown').value);if(!Number.isInteger(setpoint)||setpoint<15||setpoint>30||!Number.isInteger(duration)||duration<0||duration>10080||!Number.isInteger(manual)||manual<5||manual>100||manual%5!==0||!Number.isFinite(up)||up<0.0001||up>0.005||!Number.isFinite(down)||down<0.0001||down>0.005)throw Error('Проверьте диапазоны настроек');return{room_setpoint_milli_c:setpoint*1000,duration_ms:duration*60000,mode:q('mode').value,manual_percent:manual,slew_up_per_s:up,slew_down_per_s:down}}
function observationFor(action,payload,before){return state=>{if(action==='SETTINGS'){const v=state.settings,p=payload.settings;return v.room_setpoint_milli_c===p.room_setpoint_milli_c&&v.duration_ms===p.duration_ms&&v.mode===p.mode&&v.manual_percent===p.manual_percent&&Math.abs(v.slew_up_per_s-p.slew_up_per_s)<1e-9&&Math.abs(v.slew_down_per_s-p.slew_down_per_s)<1e-9}if(action==='START')return['PREPURGE','PREHEAT','IGNITION','VERIFY','RUN'].includes(state.state.code);if(action==='RESET_FAULT')return before.faults!==0&&state.faults===0;if(action==='ARM')return !before.delayed.armed&&state.delayed.armed;if(action==='CANCEL_ARM')return before.delayed.armed&&!state.delayed.armed;if(action==='TIMER')return payload.delay_ms===0?(before.timer.active&&!state.timer.active):(state.timer.active&&state.timer.remaining_ms<=payload.delay_ms);if(action==='CALIBRATE')return state.calibration.active;if(action==='PROCESS_CONFIG')return !before.process.configured&&state.process.configured;return false}}
async function issue(payload,dirtyIds=[]){if(!online)throw Error('Нет подтверждённой связи с автоматом');const before=lastState;const challenge=await api('/api/challenge',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!challenge.response.ok||challenge.body.status!=='CHALLENGE')throw Error(challenge.body.code||'Не удалось получить одноразовый challenge');const submitted={...payload,grant:{token:challenge.body.token,state_version:challenge.body.state_version,expires_ms:challenge.body.expires_ms}};const answer=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(submitted)});if(!answer.response.ok||answer.body.status!=='QUEUED'){report('Отклонено: '+(answer.body.code||answer.body.status));return}pending={action:payload.action,observe:observationFor(payload.action,payload,before),dirtyIds};report('Запрос принят в очередь. Настройки не считаются применёнными до чтения подтверждённого состояния.');await poll()}
async function issueSimple(action){try{await issue({action,command_id:commandId()})}catch(error){report(String(error.message||error))}}
async function stopNow(){try{const answer=await api('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'STOP',command_id:commandId()})});report(answer.response.ok?'СТОП передан автомату; ожидается штатная продувка.':'СТОП отклонён: '+(answer.body.code||answer.body.status));await poll()}catch(error){report('Не удалось передать СТОП: '+String(error.message||error))}}
async function saveSettings(){try{await issue({action:'SETTINGS',command_id:commandId(),settings:settingsPayload()},['setpoint','duration','mode','manual','slewUp','slewDown'])}catch(error){report(String(error.message||error))}}
async function armStart(){try{const minutes=Number(q('armAfter').value);if(!Number.isInteger(minutes)||minutes<1||minutes>10080)throw Error('Задержка пуска: 1…10080 минут');await issue({action:'ARM',command_id:commandId(),delay_ms:minutes*60000})}catch(error){report(String(error.message||error))}}
async function cancelArm(){try{await issue({action:'CANCEL_ARM',command_id:commandId()})}catch(error){report(String(error.message||error))}}
async function setTimer(){try{const minutes=Number(q('timerAfter').value);if(!Number.isInteger(minutes)||minutes<0||minutes>10080)throw Error('Таймер: 0…10080 минут');await issue({action:'TIMER',command_id:commandId(),delay_ms:minutes*60000})}catch(error){report(String(error.message||error))}}
async function saveProcess(){try{const process=JSON.parse(q('processJson').value);if(!process||Array.isArray(process)||typeof process!=='object')throw Error('Нужен JSON-объект профиля');await issue({action:'PROCESS_CONFIG',command_id:commandId(),process})}catch(error){report('Профиль не передан: '+String(error.message||error))}}
function hex(bytes){return Array.from(new Uint8Array(bytes),b=>b.toString(16).padStart(2,'0')).join('')}
async function hashImage(raw){return globalThis.crypto&&crypto.subtle?hex(await crypto.subtle.digest('SHA-256',raw)):window.sha256(raw)}
async function uploadOta(){try{if(!online)throw Error('Нет подтверждённой связи');const file=q('otaFile').files[0];if(!file)throw Error('Выберите подписанный .sota пакет');if(!file.name.toLowerCase().endsWith('.sota'))throw Error('Нужен подписанный .sota пакет, не исходный .bin');const raw=await file.arrayBuffer();const payload={action:'OTA',command_id:commandId(),upload_size:file.size,upload_sha256:await hashImage(raw)};const challenge=await api('/api/challenge',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!challenge.response.ok||challenge.body.status!=='CHALLENGE')throw Error(challenge.body.code||'OTA challenge отклонён');report('Загрузка пакета; до проверки подписи и образа слот не активируется…');const answer=await api('/api/ota',{method:'POST',headers:{'X-Sirokko-Token':challenge.body.token,'X-Sirokko-State-Version':challenge.body.state_version,'X-Sirokko-Expires':challenge.body.expires_ms,'X-Sirokko-Command-Id':payload.command_id,'X-Sirokko-Upload-Size':String(payload.upload_size),'X-Sirokko-Upload-SHA256':payload.upload_sha256},body:file});report(answer.response.ok&&answer.body.status==='VERIFIED_REBOOTING'?'Пакет проверен; устройство перезапускается в новый OTA-слот.':'OTA отклонена: '+(answer.body.code||answer.body.status))}catch(error){report(String(error.message||error))}}
function renderEvents(events){const body=q('events');while(body.firstChild)body.removeChild(body.firstChild);for(const event of events||[]){const row=document.createElement('tr');for(const value of [event.at_ms,event.kind_ru+' ('+event.kind+')',event.state_ru+' ('+event.state+')',event.speed_code,(event.exhaust_milli_c/1000).toFixed(1)+' / '+(event.air_milli_c/1000).toFixed(1)+' °C','0x'+Number(event.faults).toString(16)]){const cell=document.createElement('td');cell.textContent=String(value);row.appendChild(cell)}body.appendChild(row)}}
function render(state){lastState=state;setOnline(true);text('state',state.state.ru+' ('+state.state.code+')');text('block',state.start_block.ru+' ('+state.start_block.code+')');text('version',state.version);text('storage',state.storage_ok?'OK':'ОШИБКА: запись/чтение не подтверждены');text('faults','Faults: 0x'+Number(state.faults).toString(16)+'\nВыходы: FAN='+state.outputs.fan_request+' GLOW='+state.outputs.glow+' FUEL='+state.outputs.fuel+' PWM='+state.outputs.speed_code);text('room',temperature(state.sensors.room));text('exhaust',temperature(state.sensors.exhaust));text('air',temperature(state.sensors.air));text('command',state.outputs.command_percent.toFixed(1)+' %; PWM '+state.outputs.speed_code);text('range',(state.range.calibrated?'калиброван ':'не калиброван ')+state.range.minimum_speed_code+'…'+state.range.maximum_speed_code+'; доля '+(state.range.working_span_fraction*100).toFixed(1)+' %');text('roomExcess',state.room_excess?'есть: автомат не гасит горение по комнате':'нет');text('runRemaining',state.timer.active?milliseconds(state.timer.remaining_ms):'не активен');text('armRemaining',state.delayed.armed?milliseconds(state.delayed.remaining_ms):'не взведён');text('purgeRemaining',state.purge.active?milliseconds(state.purge.minimum_remaining_ms):'не активна');text('purgeConditions',state.purge.active?'Датчики: '+(state.purge.sensors_fresh?'актуальны':'недостоверны')+'; температура: '+(state.purge.temperature_ready?'допускает окончание':'ещё не допускает окончание'):'' );text('calibration',(state.calibration.active?'активна':'не активна')+'; '+state.calibration.phase_ru+' / '+state.calibration.reason_ru+'; точек '+state.calibration.points+'; '+milliseconds(state.calibration.elapsed_ms));text('profileStatus','configured='+state.process.configured+'; physical_tests='+state.process.physical_tests);if(!dirty.has('setpoint'))q('setpoint').value=(state.settings.room_setpoint_milli_c/1000).toFixed(0);if(!dirty.has('duration'))q('duration').value=Math.round(state.settings.duration_ms/60000);if(!dirty.has('mode'))q('mode').value=state.settings.mode;if(!dirty.has('manual'))q('manual').value=state.settings.manual_percent;if(!dirty.has('slewUp'))q('slewUp').value=state.settings.slew_up_per_s;if(!dirty.has('slewDown'))q('slewDown').value=state.settings.slew_down_per_s;renderEvents(state.events);text('last','Последнее подтверждённое чтение: '+new Date().toLocaleTimeString()+'; state_version='+state.state_version);if(pending&&pending.observe&&pending.observe(state)){for(const id of pending.dirtyIds)dirty.delete(id);report('Изменение подтверждено новым чтением состояния.');pending=null}}
async function poll(){try{const answer=await api('/api/state',{cache:'no-store'});if(!answer.response.ok){if(answer.response.status===401){q('panel').hidden=true;q('login').hidden=false;if(pollTimer)clearInterval(pollTimer)}else setOnline(false);return}render(answer.body)}catch(_){setOnline(false);text('last','Последние значения устарели: '+new Date().toLocaleTimeString())}}
async function login(){try{q('loginButton').disabled=true;const response=await fetch('/api/login',{method:'POST',headers:{'X-Sirokko-Password':q('password').value}});q('password').value='';if(!response.ok){text('loginResult','Отказ');return}q('login').hidden=true;q('panel').hidden=false;text('loginResult','');await poll();if(pollTimer)clearInterval(pollTimer);pollTimer=setInterval(poll,1000)}catch(_){text('loginResult','Нет связи')}finally{q('loginButton').disabled=false}}
</script></body></html>)SIROKKO";

uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
uint64_t receive_clock(void*) { return now_ms(); }
int http_receive(void* context, char* buffer, size_t capacity) {
    return httpd_req_recv(static_cast<httpd_req_t*>(context), buffer, capacity);
}

bool constant_equal(const char* left, const char* right, size_t size) {
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) difference |= static_cast<uint8_t>(left[index] ^ right[index]);
    return difference == 0;
}

class JsonBuilder {
public:
    JsonBuilder(char* output, size_t capacity) : output_(output), capacity_(capacity) {
        if (!output_ || capacity_ == 0) ok_ = false;
        else output_[0] = 0;
    }

    bool ok() const { return ok_; }
    const char* data() const { return output_; }
    size_t size() const { return size_; }

    void raw(const char* text) {
        if (!text) { ok_ = false; return; }
        while (*text) append(*text++);
    }

    void character(char value) { append(value); }

    void quoted(const char* text) {
        character('"');
        if (!text) text = "";
        for (; *text; ++text) {
            const unsigned char value = static_cast<unsigned char>(*text);
            if (value == '"' || value == '\\') { character('\\'); character(static_cast<char>(value)); }
            else if (value == '\n') raw("\\n");
            else if (value == '\r') raw("\\r");
            else if (value == '\t') raw("\\t");
            else if (value < 0x20U) raw("?");
            else character(static_cast<char>(value));
        }
        character('"');
    }

    void boolean(bool value) { raw(value ? "true" : "false"); }
    void integer(int32_t value) { formatted("%ld", static_cast<long>(value)); }
    void unsigned_integer(uint32_t value) { formatted("%lu", static_cast<unsigned long>(value)); }
    void unsigned_integer(uint64_t value) { formatted("%llu", static_cast<unsigned long long>(value)); }
    void hexadecimal(uint64_t value) { formatted("%llx", static_cast<unsigned long long>(value)); }
    void real(double value) { formatted("%.8g", value); }

private:
    void append(char value) {
        if (!ok_ || size_ + 1 >= capacity_) { ok_ = false; return; }
        output_[size_++] = value;
        output_[size_] = 0;
    }

    void formatted(const char* format, ...) {
        if (!ok_ || size_ >= capacity_) { ok_ = false; return; }
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(output_ + size_, capacity_ - size_, format, arguments);
        va_end(arguments);
        if (written < 0 || static_cast<size_t>(written) >= capacity_ - size_) { ok_ = false; return; }
        size_ += static_cast<size_t>(written);
    }

    char* output_{};
    size_t capacity_{};
    size_t size_{};
    bool ok_{true};
};

const char* russian_state(sirokko::State state) {
    switch (state) {
        case sirokko::State::RECOVERY_PURGE: return "Восстановительная продувка";
        case sirokko::State::IDLE: return "Ожидание";
        case sirokko::State::PREPURGE: return "Предпродувка";
        case sirokko::State::PREHEAT: return "Накал";
        case sirokko::State::IGNITION: return "Розжиг";
        case sirokko::State::VERIFY: return "Проверка горения";
        case sirokko::State::RUN: return "Работа";
        case sirokko::State::STOP_PURGE: return "Продувка после останова";
        case sirokko::State::FAULT_PURGE: return "Аварийная продувка";
        case sirokko::State::FAULT: return "Авария";
        case sirokko::State::MAINTENANCE: return "Обслуживание";
    }
    return "Неизвестное состояние";
}

const char* russian_block(sirokko::Block block) {
    switch (block) {
        case sirokko::Block::NONE: return "Нет";
        case sirokko::Block::PROFILE_NOT_QUALIFIED: return "Нет настроенного профиля процесса";
        case sirokko::Block::RECOVERY_REQUIRED: return "Требуется восстановительная продувка";
        case sirokko::Block::SENSOR_INVALID: return "Датчики недостоверны";
        case sirokko::Block::TEMPERATURE_OUT_OF_RANGE: return "Температура вне допуска";
        case sirokko::Block::FAULT_LATCHED: return "Авария зафиксирована";
        case sirokko::Block::BUSY: return "Автомат занят";
        case sirokko::Block::MAINTENANCE: return "Обслуживание";
        case sirokko::Block::STALE_COMMAND: return "Команда устарела";
    }
    return "Неизвестный запрет";
}

const char* russian_event(const char* kind) {
    if (!kind) return "Неизвестное событие";
    if (std::strcmp(kind, "START") == 0) return "Запуск принят автоматом";
    if (std::strcmp(kind, "STOP") == 0) return "Останов принят автоматом";
    if (std::strcmp(kind, "FAULT") == 0) return "Авария";
    if (std::strcmp(kind, "TIMER_EXPIRED") == 0) return "Истёк таймер останова";
    if (std::strcmp(kind, "TIMER_ARMED") == 0) return "Таймер останова установлен";
    if (std::strcmp(kind, "TIMER_CANCELLED") == 0) return "Таймер останова отменён";
    if (std::strcmp(kind, "PROCESS_CONFIGURED") == 0) return "Профиль процесса сохранён";
    if (std::strstr(kind, "KEY0") || std::strstr(kind, "OPERATOR"))
        return "Оператор сообщил отсутствие/неустойчивость горения (KEY0)";
    return kind;
}

const char* russian_calibration(const char* code) {
    if (!code || std::strcmp(code, "NONE") == 0) return "нет причины";
    if (std::strcmp(code, "IDLE") == 0) return "ожидание";
    if (std::strstr(code, "KEY0") || std::strstr(code, "OPERATOR"))
        return "оператор сообщил отсутствие/неустойчивость горения";
    return code;
}

bool authorized(httpd_req_t* request) {
    char cookie[160]{};
    if (!g_session[0] || now_ms() >= g_session_expires_ms ||
        httpd_req_get_hdr_value_str(request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        if (g_session[0] && now_ms() >= g_session_expires_ms) {
            g_session[0] = 0;
            g_session_expires_ms = 0;
            g_challenge.clear();
        }
        return false;
    }
    const char* value = std::strstr(cookie, "sirokko_session=");
    if (!value) return false;
    value += std::strlen("sirokko_session=");
    return std::strlen(value) >= 32 && constant_equal(value, g_session, 32) && (value[32] == 0 || value[32] == ';');
}

void no_store(httpd_req_t* request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request, "Content-Security-Policy",
                       "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
}

esp_err_t send_json(httpd_req_t* request, const char* body, const char* status = nullptr) {
    no_store(request);
    if (status) httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, body);
}

esp_err_t reject(httpd_req_t* request, const char* code, const char* status = "400 Bad Request") {
    char body[96]{};
    const int length = std::snprintf(body, sizeof(body), "{\"status\":\"REJECTED\",\"code\":\"%s\"}", code ? code : "ERROR");
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(body)) return ESP_FAIL;
    return send_json(request, body, status);
}

struct BodySink {
    char* output{};
    size_t capacity{};
    size_t size{};
};

bool consume_body(void* context, const char* buffer, size_t size) {
    auto& sink = *static_cast<BodySink*>(context);
    if (!buffer || sink.size >= sink.capacity || size >= sink.capacity - sink.size) return false;
    std::memcpy(sink.output + sink.size, buffer, size);
    sink.size += size;
    return true;
}

bool read_body(httpd_req_t* request, char* output, size_t capacity, size_t* actual_size) {
    if(!actual_size) return false;
    *actual_size=0;
    if (!request || !output || request->content_len <= 0 || static_cast<size_t>(request->content_len) >= capacity) return false;
    std::array<char, 256> chunk{};
    BodySink sink{output, capacity, 0};
    const BoundedReceiveResult result = receive_exact_bounded(
        static_cast<size_t>(request->content_len), kCommandBodyReadBudgetMs, HTTPD_SOCK_ERR_TIMEOUT,
        chunk.data(), chunk.size(), request, http_receive, &sink, consume_body, nullptr, receive_clock);
    if (result != BoundedReceiveResult::COMPLETE || sink.size != static_cast<size_t>(request->content_len)) return false;
    if(std::memchr(output,0,sink.size)) return false;
    *actual_size=sink.size;
    output[sink.size] = 0;
    return true;
}

bool parse_hex64(const char* text, uint64_t* value) {
    if (!text || !value || !*text || std::strlen(text) > 16) return false;
    uint64_t parsed = 0;
    for (const char* current = text; *current; ++current) {
        const char character = *current;
        int digit = -1;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        else return false;
        parsed = (parsed << 4U) | static_cast<uint8_t>(digit);
    }
    *value = parsed;
    return true;
}

bool parse_decimal_u64(const char* text, uint64_t* value) {
    if (!text || !value || !*text) return false;
    uint64_t parsed = 0;
    const bool leading_zero = text[0] == '0';
    for (size_t index = 0; text[index]; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        if (leading_zero && index != 0) return false;
        const uint8_t digit = static_cast<uint8_t>(text[index] - '0');
        if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

bool parse_hex_digest(const char* text, web_protocol::Digest* digest) {
    if (!text || !digest || std::strlen(text) != digest->size() * 2U) return false;
    const auto nibble = [](char character) {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    for (size_t index = 0; index < digest->size(); ++index) {
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        (*digest)[index] = static_cast<uint8_t>((high << 4U) | low);
    }
    return true;
}

bool read_header(httpd_req_t* request, const char* name, char* output, size_t capacity) {
    return request && name && output && capacity > 1 &&
           httpd_req_get_hdr_value_str(request, name, output, capacity) == ESP_OK;
}

bool semantic_valid(const web_protocol::Command& command) {
    switch (command.action) {
        case sirokko::Action::ARM:
            return command.delay_ms != 0 && command.delay_ms <= sirokko::kMaximumTimerMs;
        case sirokko::Action::TIMER:
            return command.delay_ms <= sirokko::kMaximumTimerMs;
        case sirokko::Action::SETTINGS:
            return sirokko::valid_settings(command.settings);
        case sirokko::Action::PROCESS_CONFIG:
            return sirokko::valid_process_profile(command.process) && runtime_process_valid(command.process);
        case sirokko::Action::OTA:
            return command.upload_size >= ota_verify::kManifestSize + ota_verify::kSignatureSize;
        case sirokko::Action::START:
        case sirokko::Action::RESET_FAULT:
        case sirokko::Action::CALIBRATE:
        case sirokko::Action::CANCEL_ARM:
            return true;
    }
    return false;
}

uint64_t runtime_hash(const web_protocol::Command& command) {
    return command.action == sirokko::Action::PROCESS_CONFIG
        ? sirokko::process_fingerprint(command.process)
        : web_protocol::runtime_parameters_hash(command);
}

sirokko::Grant to_runtime_grant(const web_protocol::Command& command) {
    sirokko::Grant grant{};
    grant.token = command.grant.token;
    grant.state_version = command.grant.state_version;
    grant.params_hash = runtime_hash(command);
    grant.expires_ms = command.grant.expires_ms;
    grant.action = command.action;
    grant.valid = command.has_grant && grant.token != 0;
    return grant;
}

void append_sensor(JsonBuilder* json, const sirokko::SensorSample& sample) {
    json->raw("{\"milli_c\":"); json->integer(sample.milli_c);
    json->raw(",\"sample_ms\":"); json->unsigned_integer(sample.sample_ms);
    json->raw(",\"status\":"); json->unsigned_integer(static_cast<uint32_t>(sample.status));
    json->raw(",\"valid\":"); json->boolean(sample.valid); json->character('}');
}

bool build_state_json(const RuntimeView& view, JsonBuilder* json) {
    const sirokko::Snapshot& control = view.control;
    const esp_app_desc_t* description = esp_app_get_description();
    json->raw("{\"version\":"); json->quoted(description ? description->version : "unknown");
    json->raw(",\"uptime_ms\":"); json->unsigned_integer(view.now_ms);
    json->raw(",\"state_version\":\""); json->hexadecimal(control.state_version); json->raw("\"");
    json->raw(",\"state\":{\"code\":"); json->quoted(sirokko::state_name(control.state));
    json->raw(",\"ru\":"); json->quoted(russian_state(control.state)); json->raw("}");
    json->raw(",\"start_block\":{\"code\":"); json->quoted(sirokko::block_name(control.start_block));
    json->raw(",\"ru\":"); json->quoted(russian_block(control.start_block)); json->raw("}");
    json->raw(",\"faults\":"); json->unsigned_integer(control.faults);
    json->raw(",\"storage_ok\":"); json->boolean(view.storage_ok);
    json->raw(",\"sensors\":{\"exhaust\":"); append_sensor(json, view.sensors.exhaust);
    json->raw(",\"air\":"); append_sensor(json, view.sensors.air);
    json->raw(",\"room\":"); append_sensor(json, view.sensors.room); json->raw("}");
    json->raw(",\"outputs\":{\"fan_request\":"); json->boolean(control.outputs.fan_request);
    json->raw(",\"speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(control.outputs.speed_code));
    json->raw(",\"glow\":"); json->boolean(control.outputs.glow);
    json->raw(",\"fuel\":"); json->boolean(control.outputs.fuel);
    json->raw(",\"command_percent\":"); json->real(control.command_percent); json->raw("}");
    json->raw(",\"settings\":{\"room_setpoint_milli_c\":"); json->integer(view.settings.room_setpoint_milli_c);
    json->raw(",\"duration_ms\":"); json->unsigned_integer(view.settings.duration_ms);
    json->raw(",\"mode\":"); json->quoted(view.settings.mode == sirokko::Mode::TEMPERATURE ? "TEMPERATURE" : "MANUAL");
    json->raw(",\"manual_percent\":"); json->unsigned_integer(static_cast<uint32_t>(view.settings.manual_percent));
    json->raw(",\"slew_up_per_s\":"); json->real(view.settings.slew_up_per_s);
    json->raw(",\"slew_down_per_s\":"); json->real(view.settings.slew_down_per_s); json->raw("}");
    json->raw(",\"timer\":{\"active\":"); json->boolean(control.duration_active);
    json->raw(",\"remaining_ms\":"); json->unsigned_integer(control.duration_remaining_ms);
    json->raw(",\"saved_duration_ms\":"); json->unsigned_integer(view.settings.duration_ms); json->raw("}");
    json->raw(",\"delayed\":{\"armed\":"); json->boolean(control.delayed_armed);
    json->raw(",\"remaining_ms\":"); json->unsigned_integer(control.delayed_remaining_ms); json->raw("}");
    json->raw(",\"purge\":{\"active\":"); json->boolean(control.purge_active);
    json->raw(",\"minimum_remaining_ms\":"); json->unsigned_integer(control.purge_minimum_remaining_ms);
    json->raw(",\"sensors_fresh\":"); json->boolean(control.purge_sensors_fresh);
    json->raw(",\"temperature_ready\":"); json->boolean(control.purge_temperature_ready); json->raw("}");
    json->raw(",\"range\":{\"calibrated\":"); json->boolean(control.range_calibrated);
    json->raw(",\"minimum_speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(control.minimum_speed_code));
    json->raw(",\"maximum_speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(control.maximum_speed_code));
    json->raw(",\"working_span_fraction\":"); json->real(control.working_span_fraction); json->raw("}");
    json->raw(",\"room_excess\":"); json->boolean(control.room_excess);
    json->raw(",\"process\":{\"configured\":"); json->boolean(view.process.qualified);
    json->raw(",\"physical_tests\":false}");
    json->raw(",\"calibration\":{\"active\":"); json->boolean(control.calibration_active);
    json->raw(",\"phase\":"); json->quoted(control.calibration_phase);
    json->raw(",\"phase_ru\":"); json->quoted(russian_calibration(control.calibration_phase));
    json->raw(",\"reason\":"); json->quoted(control.calibration_reason);
    json->raw(",\"reason_ru\":"); json->quoted(russian_calibration(control.calibration_reason));
    json->raw(",\"target_code\":"); json->unsigned_integer(static_cast<uint32_t>(control.calibration_target_code));
    json->raw(",\"points\":"); json->unsigned_integer(static_cast<uint32_t>(control.calibration_points));
    json->raw(",\"elapsed_ms\":"); json->unsigned_integer(control.calibration_elapsed_ms); json->raw("}");
    json->raw(",\"events\":[");
    const size_t event_count = std::min(view.event_count, view.events.size());
    for (size_t index = 0; index < event_count; ++index) {
        const sirokko::ControlEvent& event = view.events[index];
        if (index) json->character(',');
        json->raw("{\"at_ms\":"); json->unsigned_integer(event.at_ms);
        json->raw(",\"kind\":"); json->quoted(event.kind);
        json->raw(",\"kind_ru\":"); json->quoted(russian_event(event.kind));
        json->raw(",\"state\":"); json->quoted(sirokko::state_name(event.state));
        json->raw(",\"state_ru\":"); json->quoted(russian_state(event.state));
        json->raw(",\"speed_code\":"); json->unsigned_integer(static_cast<uint32_t>(event.speed_code));
        json->raw(",\"exhaust_milli_c\":"); json->integer(event.exhaust_milli_c);
        json->raw(",\"air_milli_c\":"); json->integer(event.air_milli_c);
        json->raw(",\"faults\":"); json->unsigned_integer(event.faults); json->character('}');
    }
    json->raw("]}");
    return json->ok();
}

bool target_is_esp32s3() {
    esp_chip_info_t info{};
    esp_chip_info(&info);
    return info.model == CHIP_ESP32S3;
}

bool app_project_matches(const esp_app_desc_t& description) {
    std::array<char, sizeof(description.project_name)> expected{};
    std::snprintf(expected.data(), expected.size(), "%s", kProjectName);
    return std::memcmp(description.project_name, expected.data(), expected.size()) == 0;
}

struct OtaSink {
    const esp_partition_t* partition{};
    size_t expected_size{};
    web_protocol::Digest expected_upload_digest{};
    std::array<uint8_t, ota_verify::kManifestSize> header{};
    std::array<uint8_t, ota_verify::kSignatureSize> signature{};
    std::array<uint8_t, sizeof(esp_image_header_t)> image_header{};
    ota_verify::Manifest manifest{};
    size_t received{};
    size_t header_size{};
    size_t image_size{};
    size_t signature_size{};
    size_t image_header_size{};
    esp_ota_handle_t handle{};
    bool begun{};
    bool ended{};
    bool hashes_ready{};
    mbedtls_md_context_t package_hash{};
    mbedtls_md_context_t image_hash{};
    ota_verify::Result failure{ota_verify::Result::OK};
};

bool start_hash(mbedtls_md_context_t* context) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!context || !info) return false;
    mbedtls_md_init(context);
    return mbedtls_md_setup(context, info, 0) == 0 && mbedtls_md_starts(context) == 0;
}

bool init_ota_sink(OtaSink* sink) {
    if (!sink || !start_hash(&sink->package_hash)) return false;
    if (!start_hash(&sink->image_hash)) {
        mbedtls_md_free(&sink->package_hash);
        return false;
    }
    sink->hashes_ready = true;
    return true;
}

void cleanup_ota_sink(OtaSink* sink) {
    if (!sink) return;
    if (sink->begun && !sink->ended) esp_ota_abort(sink->handle);
    sink->begun = false;
    if (sink->hashes_ready) {
        mbedtls_md_free(&sink->package_hash);
        mbedtls_md_free(&sink->image_hash);
        sink->hashes_ready = false;
    }
}

bool begin_manifest_ota(OtaSink* sink) {
    if (!sink || sink->header_size != sink->header.size()) return false;
    ota_verify::Result result = ota_verify::parse_manifest(sink->header.data(), sink->header.size(), &sink->manifest);
    if (result == ota_verify::Result::OK) {
        result = ota_verify::validate_manifest(sink->manifest, sink->expected_size, kProjectName, kTargetName,
                                               sink->partition ? sink->partition->size : 0);
    }
    if (result != ota_verify::Result::OK || !target_is_esp32s3()) {
        sink->failure = result == ota_verify::Result::OK ? ota_verify::Result::TARGET : result;
        return false;
    }
    if (esp_ota_begin(sink->partition, sink->manifest.image_size, &sink->handle) != ESP_OK) {
        sink->failure = ota_verify::Result::IMAGE;
        return false;
    }
    sink->begun = true;
    return true;
}

bool consume_ota(void* context, const char* data, size_t size) {
    auto& sink = *static_cast<OtaSink*>(context);
    if (!data || !sink.hashes_ready || !runtime_maintenance_safe()) {
        sink.failure = ota_verify::Result::INPUT;
        return false;
    }
    if (sink.received > sink.expected_size || size > sink.expected_size - sink.received) {
        sink.failure = ota_verify::Result::SIZE;
        return false;
    }
    if (mbedtls_md_update(&sink.package_hash, reinterpret_cast<const uint8_t*>(data), size) != 0) {
        sink.failure = ota_verify::Result::INPUT;
        return false;
    }
    size_t offset = 0;
    while (offset < size) {
        if (sink.header_size < sink.header.size()) {
            const size_t copied = std::min(sink.header.size() - sink.header_size, size - offset);
            std::memcpy(sink.header.data() + sink.header_size, data + offset, copied);
            sink.header_size += copied;
            offset += copied;
            if (sink.header_size == sink.header.size() && !begin_manifest_ota(&sink)) return false;
            continue;
        }
        if (sink.image_size < sink.manifest.image_size) {
            const size_t copied = std::min(static_cast<size_t>(sink.manifest.image_size) - sink.image_size, size - offset);
            const uint8_t* image = reinterpret_cast<const uint8_t*>(data + offset);
            if (sink.image_header_size < sink.image_header.size()) {
                const size_t header_bytes = std::min(sink.image_header.size() - sink.image_header_size, copied);
                std::memcpy(sink.image_header.data() + sink.image_header_size, image, header_bytes);
                sink.image_header_size += header_bytes;
                if (sink.image_header_size == sink.image_header.size()) {
                    esp_image_header_t image_header{};
                    std::memcpy(&image_header, sink.image_header.data(), sizeof(image_header));
                    if (image_header.magic != ESP_IMAGE_HEADER_MAGIC) {
                        sink.failure = ota_verify::Result::IMAGE;
                        return false;
                    }
                    if (image_header.chip_id != ESP_CHIP_ID_ESP32S3) {
                        sink.failure = ota_verify::Result::TARGET;
                        return false;
                    }
                }
            }
            if (mbedtls_md_update(&sink.image_hash, image, copied) != 0 || esp_ota_write(sink.handle, image, copied) != ESP_OK) {
                sink.failure = ota_verify::Result::IMAGE;
                return false;
            }
            sink.image_size += copied;
            offset += copied;
            continue;
        }
        if (sink.signature_size < sink.signature.size()) {
            const size_t copied = std::min(sink.signature.size() - sink.signature_size, size - offset);
            std::memcpy(sink.signature.data() + sink.signature_size, data + offset, copied);
            sink.signature_size += copied;
            offset += copied;
            continue;
        }
        sink.failure = ota_verify::Result::SIZE;
        return false;
    }
    sink.received += size;
    return true;
}

ota_verify::Result finish_ota_sink(OtaSink* sink) {
    if (!sink || !sink->hashes_ready || sink->received != sink->expected_size ||
        sink->header_size != sink->header.size() || sink->image_size != sink->manifest.image_size ||
        sink->signature_size != sink->signature.size()) return ota_verify::Result::TRUNCATED;
    web_protocol::Digest package_digest{};
    web_protocol::Digest image_digest{};
    if (mbedtls_md_finish(&sink->package_hash, package_digest.data()) != 0 ||
        mbedtls_md_finish(&sink->image_hash, image_digest.data()) != 0) return ota_verify::Result::INPUT;
    if (!web_protocol::digest_equal(package_digest, sink->expected_upload_digest)) return ota_verify::Result::DIGEST;
    if (!web_protocol::digest_equal(image_digest, sink->manifest.image_sha256)) return ota_verify::Result::DIGEST;
    const ota_verify::Result signature_result = ota_verify::verify_signature(
        sink->manifest, sink->signature.data(), sink->signature.size(), ota_public_key_pem_start,
        static_cast<size_t>(ota_public_key_pem_end - ota_public_key_pem_start));
    if (signature_result != ota_verify::Result::OK) return signature_result;
    if (esp_ota_end(sink->handle) != ESP_OK) return ota_verify::Result::IMAGE;
    sink->ended = true;
    esp_app_desc_t description{};
    if (esp_ota_get_partition_description(sink->partition, &description) != ESP_OK || !app_project_matches(description)) {
        return ota_verify::Result::PROJECT;
    }
    return ota_verify::Result::OK;
}

esp_err_t page_handler(httpd_req_t* request) {
    no_store(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t login_handler(httpd_req_t* request) {
    const uint64_t current = now_ms();
    if ((g_failed_logins >= 5 && current - g_last_login_ms < 30000) || current - g_last_login_ms < 500) {
        return reject(request, "RATE_LIMIT", "429 Too Many Requests");
    }
    g_last_login_ms = current;
    char supplied[32]{};
    const bool header = read_header(request, "X-Sirokko-Password", supplied, sizeof(supplied));
    const bool accepted = header && std::strlen(supplied) == 20 && constant_equal(supplied, g_password, 20);
    std::memset(supplied, 0, sizeof(supplied));
    if (!accepted) {
        ++g_failed_logins;
        return reject(request, "AUTH", "401 Unauthorized");
    }
    g_failed_logins = 0;
    for (size_t index = 0; index < 16; ++index) {
        std::snprintf(g_session + index * 2, 3, "%02x", static_cast<unsigned>(esp_random() & 0xFFU));
    }
    g_session_expires_ms = current + kSessionLifetimeMs;
    g_challenge.clear();
    char cookie[96]{};
    std::snprintf(cookie, sizeof(cookie), "sirokko_session=%s; HttpOnly; SameSite=Strict; Path=/", g_session);
    no_store(request);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    return httpd_resp_sendstr(request, "{\"status\":\"OK\"}");
}

esp_err_t state_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    JsonBuilder json(g_state_json.data(), g_state_json.size());
    if (!build_state_json(view, &json)) return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, json.data());
}

esp_err_t challenge_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size)) return reject(request, "BODY");
    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    web_protocol::Command command{};
    const web_protocol::ParseError parsed = web_protocol::parse_challenge_request(body.data(), body_size, view.settings, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK) return reject(request, web_protocol::error_name(parsed));
    if (!semantic_valid(command)) return reject(request, "VALUE");
    sirokko::Grant issued{};
    const uint64_t parameters = runtime_hash(command);
    if (!runtime_issue(command.action, parameters, &issued)) return reject(request, "BUSY", "503 Service Unavailable");
    const web_protocol::GrantFields fields{issued.token, issued.state_version, issued.expires_ms};
    g_challenge.bind(command.action, fields, command.payload_digest);
    char response[192]{};
    const int length = std::snprintf(response, sizeof(response),
        "{\"status\":\"CHALLENGE\",\"token\":\"%llx\",\"state_version\":\"%llx\",\"expires_ms\":\"%llx\"}",
        static_cast<unsigned long long>(issued.token), static_cast<unsigned long long>(issued.state_version),
        static_cast<unsigned long long>(issued.expires_ms));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(response)) return reject(request, "ENCODING", "500 Internal Server Error");
    return send_json(request, response);
}

esp_err_t command_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    std::array<char, kCommandBodyLimit> body{};
    size_t body_size{};
    if (!read_body(request, body.data(), body.size(), &body_size)) return reject(request, "BODY");

    web_protocol::Stop stop{};
    const web_protocol::ParseError stop_parsed = web_protocol::parse_stop_request(body.data(), body_size, &stop);
    if (stop_parsed == web_protocol::ParseError::OK) {
        bool previous = false;
        const web_protocol::ReplayResult replay = g_replays.lookup(stop.command_id, stop.payload_digest, &previous);
        std::fill(body.begin(), body.end(), 0);
        if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
        if (replay == web_protocol::ReplayResult::MATCH) return send_json(request, "{\"status\":\"STOP_REQUESTED\",\"replay\":true}");
        runtime_stop(false);
        g_replays.remember(stop.command_id, stop.payload_digest, true);
        return send_json(request, "{\"status\":\"STOP_REQUESTED\"}");
    }

    RuntimeView view{};
    if (!runtime_view(&view)) { std::fill(body.begin(), body.end(), 0); return reject(request, "BUSY", "503 Service Unavailable"); }
    web_protocol::Command command{};
    const web_protocol::ParseError parsed = web_protocol::parse_command_request(body.data(), body_size, view.settings, &command);
    std::fill(body.begin(), body.end(), 0);
    if (parsed != web_protocol::ParseError::OK) return reject(request, web_protocol::error_name(parsed));
    bool previous = false;
    const web_protocol::ReplayResult replay = g_replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH) {
        return send_json(request, previous ? "{\"status\":\"QUEUED\",\"replay\":true}" : "{\"status\":\"REJECTED\",\"replay\":true}");
    }
    if (!semantic_valid(command)) return reject(request, "VALUE");
    const web_protocol::BindingResult binding = g_challenge.consume(command, view.now_ms, view.control.state_version);
    if (binding != web_protocol::BindingResult::ACCEPTED) return reject(request, "CHALLENGE", "409 Conflict");
    const sirokko::Grant grant = to_runtime_grant(command);
    const bool accepted = command.action == sirokko::Action::PROCESS_CONFIG
        ? runtime_configure_process(grant, command.process)
        : runtime_submit(grant, command.action, command.delay_ms,
                         command.action == sirokko::Action::SETTINGS ? &command.settings : nullptr);
    g_replays.remember(command.command_id, command.payload_digest, accepted);
    if (!accepted) return reject(request, "NOT_ACCEPTED", "409 Conflict");
    return send_json(request, "{\"status\":\"QUEUED\"}");
}

esp_err_t ota_handler(httpd_req_t* request) {
    if (!authorized(request)) return reject(request, "AUTH", "401 Unauthorized");
    char token_text[17]{}, version_text[17]{}, expires_text[17]{}, id_text[17]{};
    char upload_size_text[24]{}, upload_digest_text[65]{};
    if (!read_header(request, "X-Sirokko-Token", token_text, sizeof(token_text)) ||
        !read_header(request, "X-Sirokko-State-Version", version_text, sizeof(version_text)) ||
        !read_header(request, "X-Sirokko-Expires", expires_text, sizeof(expires_text)) ||
        !read_header(request, "X-Sirokko-Command-Id", id_text, sizeof(id_text)) ||
        !read_header(request, "X-Sirokko-Upload-Size", upload_size_text, sizeof(upload_size_text)) ||
        !read_header(request, "X-Sirokko-Upload-SHA256", upload_digest_text, sizeof(upload_digest_text))) return reject(request, "GRANT");

    web_protocol::Command command{};
    command.action = sirokko::Action::OTA;
    command.has_grant = parse_hex64(token_text, &command.grant.token) &&
                        parse_hex64(version_text, &command.grant.state_version) &&
                        parse_hex64(expires_text, &command.grant.expires_ms);
    command.has_grant = command.has_grant && parse_hex64(id_text, &command.command_id) && command.command_id != 0 &&
                        parse_decimal_u64(upload_size_text, &command.upload_size) &&
                        parse_hex_digest(upload_digest_text, &command.upload_sha256);
    command.payload_digest = web_protocol::compute_payload_digest(command);
    if (!command.has_grant || request->content_len <= 0 || command.upload_size != static_cast<uint64_t>(request->content_len) ||
        !semantic_valid(command)) return reject(request, "UPLOAD");

    RuntimeView view{};
    if (!runtime_view(&view)) return reject(request, "BUSY", "503 Service Unavailable");
    bool previous = false;
    const web_protocol::ReplayResult replay = g_replays.lookup(command.command_id, command.payload_digest, &previous);
    if (replay == web_protocol::ReplayResult::CONFLICT) return reject(request, "REPLAY", "409 Conflict");
    if (replay == web_protocol::ReplayResult::MATCH) return reject(request, "REPLAY", "409 Conflict");
    if (g_challenge.consume(command, view.now_ms, view.control.state_version) != web_protocol::BindingResult::ACCEPTED) {
        return reject(request, "CHALLENGE", "409 Conflict");
    }

    const sirokko::Grant grant = to_runtime_grant(command);
    if (!runtime_submit(grant, sirokko::Action::OTA, 0, nullptr)) {
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }
    const uint64_t waiting_started = now_ms();
    while (!runtime_maintenance_safe() && now_ms() - waiting_started < kMaintenanceWaitMs) vTaskDelay(pdMS_TO_TICKS(10));
    if (!runtime_maintenance_safe()) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "NOT_SAFE", "409 Conflict");
    }

    const uint32_t maintenance_lease=runtime_maintenance_lease();
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    const size_t minimum_size = ota_verify::kManifestSize + ota_verify::kSignatureSize;
    if (!partition || static_cast<size_t>(request->content_len) < minimum_size ||
        static_cast<size_t>(request->content_len) > partition->size + minimum_size) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "SIZE");
    }

    OtaSink sink{};
    sink.partition = partition;
    sink.expected_size = static_cast<size_t>(request->content_len);
    sink.expected_upload_digest = command.upload_sha256;
    if (!init_ota_sink(&sink)) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "HASH", "500 Internal Server Error");
    }
    std::array<char, 4096> buffer{};
    const BoundedReceiveResult received = receive_exact_bounded(sink.expected_size, kOtaBodyReadBudgetMs,
        HTTPD_SOCK_ERR_TIMEOUT, buffer.data(), buffer.size(), request, http_receive, &sink, consume_ota,
        nullptr, receive_clock);
    std::fill(buffer.begin(), buffer.end(), 0);
    ota_verify::Result result = received == BoundedReceiveResult::COMPLETE
        ? finish_ota_sink(&sink)
        : (sink.failure == ota_verify::Result::OK ? ota_verify::Result::TRUNCATED : sink.failure);
    if (received == BoundedReceiveResult::DEADLINE_EXCEEDED) result = ota_verify::Result::TRUNCATED;
    cleanup_ota_sink(&sink);
    if (result != ota_verify::Result::OK) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, received == BoundedReceiveResult::DEADLINE_EXCEEDED ? "OTA_TIMEOUT" : ota_verify::result_name(result),
                      "409 Conflict");
    }
    if (!runtime_activate_ota(maintenance_lease,partition)) {
        runtime_exit_maintenance();
        g_replays.remember(command.command_id, command.payload_digest, false);
        return reject(request, "MAINTENANCE_LEASE", "409 Conflict");
    }
    g_replays.remember(command.command_id, command.payload_digest, true);
    return send_json(request, "{\"status\":\"VERIFIED_ACTIVATION_QUEUED\"}");
}

}  // namespace

bool web_start(const char* ap_password) {
    if (!ap_password || std::strlen(ap_password) != 20) return false;
    std::memcpy(g_password, ap_password, sizeof(g_password));
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 5;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 5;
    httpd_handle_t server{};
    if (httpd_start(&server, &config) != ESP_OK) return false;
    const httpd_uri_t handlers[] = {
        {"/", HTTP_GET, page_handler, nullptr},
        {"/api/login", HTTP_POST, login_handler, nullptr},
        {"/api/state", HTTP_GET, state_handler, nullptr},
        {"/api/challenge", HTTP_POST, challenge_handler, nullptr},
        {"/api/command", HTTP_POST, command_handler, nullptr},
        {"/api/ota", HTTP_POST, ota_handler, nullptr},
    };
    for (const httpd_uri_t& handler : handlers) {
        if (httpd_register_uri_handler(server, &handler) != ESP_OK) return false;
    }
    return true;
}
